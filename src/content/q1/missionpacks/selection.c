/* Port of src/content/q1/missionpacks/selection.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/selection.h"
#include "missionpacks_internal.h"
#include "qa/content/q1/missionpacks/messages.h"

static const char *const HIP_CYCLE[] = {"axe","shotgun","supershotgun","nailgun","supernailgun","grenadelauncher","hipnotic:proximity","rocketlauncher","lightning","hipnotic:laser","hipnotic:mjolnir"};
static const char *const ROG_CYCLE[] = {"axe","rogue:grapple","shotgun","supershotgun","nailgun","rogue:lava-nailgun","supernailgun","rogue:lava-supernailgun","grenadelauncher","rogue:multi-grenade","rocketlauncher","rogue:multi-rocket","lightning","rogue:plasma"};
static double count_required(const char *w) { return qa_q1mp_streq(w,"supershotgun")||qa_q1mp_streq(w,"supernailgun")||qa_q1mp_streq(w,"rogue:lava-supernailgun") ? 2 : 1; }
static bool owns(qa_q1mp_game *g, qa_q1mp_player_state *p, const char *w) {
  char buf[96]; return qa_q1mp_count(g, p->actor.id, (qa_item_id)qa_q1mp_weapon_item(w, buf, sizeof buf)) > 0;
}
static bool has_ammo_req(qa_q1mp_game *g, qa_q1mp_player_state *p, const char *w, double req) {
  qa_item_id ammo = g->base->weapon_ammo(g->base, w);
  return ammo == NULL || qa_q1mp_count(g, p->actor.id, ammo) >= req;
}
static bool cycle(qa_q1mp_game *g, qa_q1mp_player_state *p, qa_q1mp_pack pack, bool reverse) {
  bool hip = qa_q1mp_streq(pack, "hipnotic");
  const char *const *order = hip ? HIP_CYCLE : ROG_CYCLE;
  int len = hip ? 11 : 14, current = -1;
  for (int i = 0; i < len; i++) if (qa_q1mp_streq(order[i], p->weapon)) current = i;
  for (int step = 1; step <= len; step++) {
    const char *next = order[((current + (reverse ? -step : step) + len * 2) % len + len) % len];
    if (qa_q1mp_streq(next, "rogue:grapple") && !(g->base->options.deathmatch != 0 && qa_q1mp_teamplay(g) >= 4)) continue;
    if (owns(g, p, next) && has_ammo_req(g, p, next, reverse && qa_q1mp_streq(next, "rogue:lava-nailgun") ? 2 : count_required(next)))
      return g->base->select_weapon(g->base, p->actor, next);
  }
  return false;
}
static const char *paired(qa_q1mp_game *g, qa_q1mp_player_state *p, const char *base, const char *powered) {
  return owns(g, p, powered) && (qa_q1mp_streq(p->weapon, base) || !has_ammo_req(g, p, base, count_required(base))) ? powered : base;
}
static const char *rogue_selected(qa_q1mp_game *g, qa_q1mp_player_state *p, int impulse) {
  bool ctf = g->base->options.deathmatch != 0 && qa_q1mp_teamplay(g) >= 4;
  bool rr = qa_q1mp_streq(g->base->options.edition, "rerelease");
  switch (impulse) {
    case 1: return ctf && qa_q1mp_streq(p->weapon, "axe") ? "rogue:grapple" : "axe";
    case 2: return "shotgun"; case 3: return "supershotgun";
    case 4: return paired(g, p, "nailgun", "rogue:lava-nailgun");
    case 5: return paired(g, p, "supernailgun", "rogue:lava-supernailgun");
    case 6: return paired(g, p, "grenadelauncher", "rogue:multi-grenade");
    case 7: return paired(g, p, "rocketlauncher", "rogue:multi-rocket");
    case 8: return paired(g, p, "lightning", "rogue:plasma");
    case 22: return ctf ? "rogue:grapple" : NULL;
    case 60: return "rogue:lava-nailgun"; case 61: return "rogue:lava-supernailgun";
    case 62: return "rogue:multi-grenade"; case 63: return "rogue:multi-rocket"; case 64: return "rogue:plasma";
    case 65: return rr ? "nailgun" : NULL; case 66: return rr ? "supernailgun" : NULL;
    case 67: return rr ? "grenadelauncher" : NULL; case 68: return rr ? "rocketlauncher" : NULL;
    default: return NULL;
  }
}
static const char *hip_selected(qa_q1mp_game *g, qa_q1mp_player_state *p, int impulse) {
  bool rr = qa_q1mp_streq(g->base->options.edition, "rerelease");
  switch (impulse) {
    case 1: return "axe"; case 2: return "shotgun"; case 3: return "supershotgun";
    case 4: return "nailgun"; case 5: return "supernailgun";
    case 6: return qa_q1mp_streq(p->weapon, "grenadelauncher") ? "hipnotic:proximity" : "grenadelauncher";
    case 7: return "rocketlauncher"; case 8: return "lightning";
    case 225: return "hipnotic:laser"; case 226: return "hipnotic:mjolnir";
    case 227: return rr ? "hipnotic:proximity" : NULL; case 228: return rr ? "grenadelauncher" : NULL;
    default: return NULL;
  }
}
bool qa_q1mp_mission_weapon_impulse(qa_q1mp_game *game, qa_q1mp_player_state *player, qa_q1mp_pack pack, double impulse_d) {
  int impulse = (int)impulse_d; bool hip = qa_q1mp_streq(pack, "hipnotic");
  if (impulse == 10 || impulse == 12) { (void)cycle(game, player, pack, impulse == 12); return true; }
  const char *selected = hip ? hip_selected(game, player, impulse) : rogue_selected(game, player, impulse);
  if (selected == NULL) return false;
  if (hip && qa_q1mp_streq(selected, "grenadelauncher") && !owns(game, player, selected)) selected = "hipnotic:proximity";
  if (!owns(game, player, selected)) { qa_q1mp_mission_message(game, QA_Q1MP_SOME(player->actor.id), "$qc_no_weapon"); return true; }
  double required = !hip && impulse == 61 ? 1 : count_required(selected);
  bool has_alias = false; double alias_ammo = 0;
  if (!hip && impulse >= 65 && impulse <= 68 && qa_q1mp_starts(player->weapon, "rogue:") && !qa_q1mp_streq(player->weapon, "rogue:grapple")) {
    has_alias = true; alias_ammo = qa_q1mp_count(game, player->actor.id, impulse <= 66 ? "rogue:ammo/lava-nails" : "rogue:ammo/multi-rockets");
  }
  if (!has_alias ? !has_ammo_req(game, player, selected, required) : alias_ammo < required) { qa_q1mp_mission_message(game, QA_Q1MP_SOME(player->actor.id), "$qc_not_enough_ammo"); return true; }
  if (!hip && !qa_q1mp_streq(selected, player->weapon)) {
    const char *old = player->weapon, *key = "";
    if (qa_q1mp_streq(old,"rogue:lava-nailgun")||qa_q1mp_streq(old,"rogue:lava-supernailgun")) key = (qa_q1mp_streq(selected,"nailgun")||qa_q1mp_streq(selected,"supernailgun")) ? "$qc_normal_nails" : "";
    else if (qa_q1mp_streq(old,"rogue:multi-grenade")) key = qa_q1mp_streq(selected,"grenadelauncher") ? "$qc_normal_grenades" : "";
    else if (qa_q1mp_streq(old,"rogue:multi-rocket")) key = qa_q1mp_streq(selected,"rocketlauncher") ? "$qc_normal_rockets" : "";
    else if (qa_q1mp_streq(old,"rogue:plasma")) key = qa_q1mp_streq(selected,"lightning") ? "$qc_lightning_gun" : "";
    else if (qa_q1mp_streq(selected,"rogue:lava-nailgun")||qa_q1mp_streq(selected,"rogue:lava-supernailgun")) key = "$qc_lava_nails";
    else if (qa_q1mp_streq(selected,"rogue:multi-grenade")) key = "$qc_multi_gl";
    else if (qa_q1mp_streq(selected,"rogue:multi-rocket")) key = "$qc_multi_rl";
    else if (qa_q1mp_streq(selected,"rogue:plasma")) key = "$qc_plasma_gun";
    if (key[0]) qa_q1mp_mission_message(game, QA_Q1MP_SOME(player->actor.id), key);
  }
  (void)game->base->select_weapon(game->base, player->actor, selected); return true;
}
