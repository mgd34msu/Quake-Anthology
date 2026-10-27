/* Port of src/content/q1/missionpacks/arsenal.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/arsenal.h"
#include "missionpacks_internal.h"
#include "qa/content/q1/missionpacks/backpacks.h"
#include "qa/content/q1/missionpacks/hipnotic_weapons.h"
#include "qa/content/q1/missionpacks/items.h"
#include "qa/content/q1/missionpacks/pickup_rules.h"
#include "qa/content/q1/missionpacks/rogue_weapons.h"
#include "qa/content/q1/missionpacks/selection.h"
#include <math.h>
#include <stdlib.h>

typedef struct fire_ctx { qa_q1mp_game *game; const char *weapon; } fire_ctx;
static bool weapon_fire(void *vctx, qa_q1mp_game *game, qa_q1mp_player_state *player) {
  const char *weapon = ((fire_ctx *)vctx)->weapon;
  if (qa_q1mp_streq(weapon,"hipnotic:laser")) return qa_q1mp_fire_hipnotic_laser(game, player);
  if (qa_q1mp_streq(weapon,"hipnotic:mjolnir")) return qa_q1mp_fire_hipnotic_mjolnir(game, player);
  if (qa_q1mp_streq(weapon,"hipnotic:proximity")) return qa_q1mp_fire_hipnotic_proximity(game, player);
  if (qa_q1mp_streq(weapon,"rogue:lava-nailgun")||qa_q1mp_streq(weapon,"rogue:lava-supernailgun")) return qa_q1mp_fire_rogue_lava(game, player);
  if (qa_q1mp_streq(weapon,"rogue:multi-grenade")) return qa_q1mp_fire_rogue_multi_grenade(game, player);
  if (qa_q1mp_streq(weapon,"rogue:multi-rocket")) return qa_q1mp_fire_rogue_multi_rocket(game, player);
  return qa_q1mp_fire_rogue_plasma(game, player);
}
static bool best_available(void *vctx, qa_q1mp_game *game, qa_q1mp_player_state *player) {
  const char *weapon = ((fire_ctx *)vctx)->weapon;
  return !qa_q1mp_streq(weapon,"rogue:lava-supernailgun") || qa_q1mp_count(game, player->actor.id, "rogue:ammo/lava-nails") >= 2;
}
static void animate(void *vctx, qa_q1mp_game *game, qa_q1mp_player_state *player, double seconds) {
  (void)vctx;
  if (player->continuous_firing || player->weapon_animation_at < 0) return;
  double step = floor((seconds - player->weapon_animation_at) / 0.1);
  double frame = step >= 6 ? 0 : qa_q1mp_streq(player->weapon,"hipnotic:mjolnir") ? fmin(4, step + 1) : step + 1;
  if (frame != player->weapon_frame) {
    player->weapon_frame = frame;
    if (game->emit_weapon) game->emit_weapon(game->ctx, player->actor.id, player->weapon, game->weapon_model ? game->weapon_model(game->ctx, player->weapon) : "", frame, 0);
  }
  if (step >= 6) player->weapon_animation_at = -1;
}
static void register_weapon_definitions(qa_q1mp_game *game, qa_q1mp_pack pack) {
  char prefix[32]; (void)snprintf(prefix, sizeof prefix, "%s:", pack);
  for (size_t i = 0; i < QA_Q1MP_MISSION_WEAPONS_COUNT; i++) {
    const qa_q1mp_mission_weapon_definition *w = &qa_q1mp_mission_weapons[i];
    if (!qa_q1mp_starts(w->id, prefix)) continue;
    fire_ctx *c = malloc(sizeof *c); c->game = game; c->weapon = w->id;
    if (game->register_weapon) {
      qa_q1mp_weapon_registration reg = {0};
      reg.id = w->id; reg.ammo = w->ammo; reg.model = w->model; reg.rank = w->rank; reg.ctx = c;
      reg.best_available = best_available; reg.fire = weapon_fire; reg.animate = animate;
      game->register_weapon(game->ctx, &reg);
    }
  }
  static const char *const hip_order[] = {"lightning","hipnotic:laser","supernailgun","supershotgun","nailgun","shotgun","hipnotic:mjolnir","axe"};
  static const char *const rog_order[] = {"lightning","rogue:lava-supernailgun","supernailgun","rogue:lava-nailgun","nailgun","supershotgun","shotgun","axe"};
  if (game->register_weapon_order) {
    char id[64]; (void)snprintf(id, sizeof id, "q1:%s", pack);
    game->register_weapon_order(game->ctx, id, qa_q1mp_streq(pack,"hipnotic") ? hip_order : rog_order, 8);
  }
}
void qa_q1mp_register_hipnotic_weapons(qa_q1mp_game *game) {
  qa_q1mp_register_hipnotic_weapon_callbacks(game);
  register_weapon_definitions(game, "hipnotic");
}
typedef struct svc_ctx { qa_q1mp_mission_pack_arsenal *arsenal; } svc_ctx;
static void svc_powerup(void *vctx, qa_q1mp_player_state *p, const char *pw, double s) { qa_q1mp_players_powerup(&((svc_ctx *)vctx)->arsenal->players, p, pw, s); }
static bool svc_sphere(void *vctx, qa_q1mp_actor *i, qa_q1mp_player_state *p) { return qa_q1mp_players_sphere(&((svc_ctx *)vctx)->arsenal->players, i, p); }
static void svc_combos(void *vctx, qa_q1mp_player_state *p) { qa_q1mp_players_enable_combos(&((svc_ctx *)vctx)->arsenal->players, p); }
static void svc_horn(void *vctx, qa_q1mp_actor *item, qa_q1mp_player_state *player) {
  qa_q1mp_mission_pack_arsenal *a = ((svc_ctx *)vctx)->arsenal;
  qa_q1mp_opt_actor previous = a->charmer; a->charmer = QA_Q1MP_SOME(player->actor.id);
  a->game->base->use_targets(a->game->base, item, QA_Q1MP_SOME(player->actor.id));
  a->charmer = previous;
}
void qa_q1mp_mission_pack_arsenal_init(qa_q1mp_mission_pack_arsenal *arsenal, qa_q1mp_game *game, qa_q1mp_pack pack) {
  arsenal->game = game; arsenal->pack = pack; arsenal->charmer = QA_Q1MP_NONE; arsenal->grapple = NULL;
  qa_q1mp_mission_pack_players_init(&arsenal->players, game, pack);
  qa_q1mp_register_mission_pack_pickup_rules(game, pack, &arsenal->players);
  if (qa_q1mp_streq(pack, "hipnotic")) qa_q1mp_register_hipnotic_weapons(game);
  else {
    qa_q1mp_register_rogue_weapon_callbacks(game);
    qa_q1mp_register_rogue_toss_callbacks(game, &arsenal->players);
    qa_q1mp_rogue_grapple_init(&arsenal->grapple_storage, game); arsenal->grapple = &arsenal->grapple_storage;
    register_weapon_definitions(game, pack);
  }
  svc_ctx *c = malloc(sizeof *c); c->arsenal = arsenal;
  qa_q1mp_mission_item_services services = {pack, c, svc_horn, svc_sphere, svc_powerup, svc_combos};
  qa_q1mp_register_mission_pack_items(game, &services);
}
qa_q1mp_mission_pack_arsenal *qa_q1mp_register_mission_pack_arsenal(qa_q1mp_game *game, qa_q1mp_pack pack, qa_q1mp_mission_pack_arsenal *out) {
  qa_q1mp_mission_pack_arsenal_init(out, game, pack); return out;
}
bool qa_q1mp_arsenal_impulse(qa_q1mp_mission_pack_arsenal *arsenal, qa_actor_id actor, double impulse) {
  qa_q1mp_player_state *player = arsenal->game->base->player(arsenal->game->base, actor);
  if (!player) return false;
  if (qa_q1mp_streq(arsenal->pack,"rogue") && (impulse == 20 || impulse == 21)) {
    if (impulse == 20) (void)qa_q1mp_toss_rogue_backpack(arsenal->game, player);
    else (void)qa_q1mp_toss_rogue_weapon(arsenal->game, player);
    return true;
  }
  qa_q1mp_players_enable_combos(&arsenal->players, player);
  return qa_q1mp_mission_weapon_impulse(arsenal->game, player, arsenal->pack, impulse);
}
qa_q1mp_opt_actor qa_q1mp_arsenal_horn_charmer(qa_q1mp_mission_pack_arsenal *arsenal) { return arsenal->charmer; }
