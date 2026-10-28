/* Port of src/content/q3/base/shared/items.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/shared/items.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "qa/core/common_error.h"
#include "qa/foundation/support.h"

enum { ITEM_COUNT = 52, BASE_ITEM_COUNT = 36 };
static const qa_q3_base_item_definition DEFINITIONS[ITEM_COUNT] = {
  {NULL,NULL,{NULL,NULL,NULL,NULL},NULL,NULL,0,0,0,"",""},
  {"item_armor_shard","sound/misc/ar1_pkup.wav",{"models/powerups/armor/shard.md3","models/powerups/armor/shard_sphere.md3",NULL,NULL},"icons/iconr_shard","Armor Shard",5,3,0,"",""},
  {"item_armor_combat","sound/misc/ar2_pkup.wav",{"models/powerups/armor/armor_yel.md3",NULL,NULL,NULL},"icons/iconr_yellow","Armor",50,3,0,"",""},
  {"item_armor_body","sound/misc/ar2_pkup.wav",{"models/powerups/armor/armor_red.md3",NULL,NULL,NULL},"icons/iconr_red","Heavy Armor",100,3,0,"",""},
  {"item_health_small","sound/items/s_health.wav",{"models/powerups/health/small_cross.md3","models/powerups/health/small_sphere.md3",NULL,NULL},"icons/iconh_green","5 Health",5,4,0,"",""},
  {"item_health","sound/items/n_health.wav",{"models/powerups/health/medium_cross.md3","models/powerups/health/medium_sphere.md3",NULL,NULL},"icons/iconh_yellow","25 Health",25,4,0,"",""},
  {"item_health_large","sound/items/l_health.wav",{"models/powerups/health/large_cross.md3","models/powerups/health/large_sphere.md3",NULL,NULL},"icons/iconh_red","50 Health",50,4,0,"",""},
  {"item_health_mega","sound/items/m_health.wav",{"models/powerups/health/mega_cross.md3","models/powerups/health/mega_sphere.md3",NULL,NULL},"icons/iconh_mega","Mega Health",100,4,0,"",""},
  {"weapon_gauntlet","sound/misc/w_pkup.wav",{"models/weapons2/gauntlet/gauntlet.md3",NULL,NULL,NULL},"icons/iconw_gauntlet","Gauntlet",0,1,1,"",""},
  {"weapon_shotgun","sound/misc/w_pkup.wav",{"models/weapons2/shotgun/shotgun.md3",NULL,NULL,NULL},"icons/iconw_shotgun","Shotgun",10,1,3,"",""},
  {"weapon_machinegun","sound/misc/w_pkup.wav",{"models/weapons2/machinegun/machinegun.md3",NULL,NULL,NULL},"icons/iconw_machinegun","Machinegun",40,1,2,"",""},
  {"weapon_grenadelauncher","sound/misc/w_pkup.wav",{"models/weapons2/grenadel/grenadel.md3",NULL,NULL,NULL},"icons/iconw_grenade","Grenade Launcher",10,1,4,"","sound/weapons/grenade/hgrenb1a.wav sound/weapons/grenade/hgrenb2a.wav"},
  {"weapon_rocketlauncher","sound/misc/w_pkup.wav",{"models/weapons2/rocketl/rocketl.md3",NULL,NULL,NULL},"icons/iconw_rocket","Rocket Launcher",10,1,5,"",""},
  {"weapon_lightning","sound/misc/w_pkup.wav",{"models/weapons2/lightning/lightning.md3",NULL,NULL,NULL},"icons/iconw_lightning","Lightning Gun",100,1,6,"",""},
  {"weapon_railgun","sound/misc/w_pkup.wav",{"models/weapons2/railgun/railgun.md3",NULL,NULL,NULL},"icons/iconw_railgun","Railgun",10,1,7,"",""},
  {"weapon_plasmagun","sound/misc/w_pkup.wav",{"models/weapons2/plasma/plasma.md3",NULL,NULL,NULL},"icons/iconw_plasma","Plasma Gun",50,1,8,"",""},
  {"weapon_bfg","sound/misc/w_pkup.wav",{"models/weapons2/bfg/bfg.md3",NULL,NULL,NULL},"icons/iconw_bfg","BFG10K",20,1,9,"",""},
  {"weapon_grapplinghook","sound/misc/w_pkup.wav",{"models/weapons2/grapple/grapple.md3",NULL,NULL,NULL},"icons/iconw_grapple","Grappling Hook",0,1,10,"",""},
  {"ammo_shells","sound/misc/am_pkup.wav",{"models/powerups/ammo/shotgunam.md3",NULL,NULL,NULL},"icons/icona_shotgun","Shells",10,2,3,"",""},
  {"ammo_bullets","sound/misc/am_pkup.wav",{"models/powerups/ammo/machinegunam.md3",NULL,NULL,NULL},"icons/icona_machinegun","Bullets",50,2,2,"",""},
  {"ammo_grenades","sound/misc/am_pkup.wav",{"models/powerups/ammo/grenadeam.md3",NULL,NULL,NULL},"icons/icona_grenade","Grenades",5,2,4,"",""},
  {"ammo_cells","sound/misc/am_pkup.wav",{"models/powerups/ammo/plasmaam.md3",NULL,NULL,NULL},"icons/icona_plasma","Cells",30,2,8,"",""},
  {"ammo_lightning","sound/misc/am_pkup.wav",{"models/powerups/ammo/lightningam.md3",NULL,NULL,NULL},"icons/icona_lightning","Lightning",60,2,6,"",""},
  {"ammo_rockets","sound/misc/am_pkup.wav",{"models/powerups/ammo/rocketam.md3",NULL,NULL,NULL},"icons/icona_rocket","Rockets",5,2,5,"",""},
  {"ammo_slugs","sound/misc/am_pkup.wav",{"models/powerups/ammo/railgunam.md3",NULL,NULL,NULL},"icons/icona_railgun","Slugs",10,2,7,"",""},
  {"ammo_bfg","sound/misc/am_pkup.wav",{"models/powerups/ammo/bfgam.md3",NULL,NULL,NULL},"icons/icona_bfg","Bfg Ammo",15,2,9,"",""},
  {"holdable_teleporter","sound/items/holdable.wav",{"models/powerups/holdable/teleporter.md3",NULL,NULL,NULL},"icons/teleporter","Personal Teleporter",60,6,1,"",""},
  {"holdable_medkit","sound/items/holdable.wav",{"models/powerups/holdable/medkit.md3","models/powerups/holdable/medkit_sphere.md3",NULL,NULL},"icons/medkit","Medkit",60,6,2,"","sound/items/use_medkit.wav"},
  {"item_quad","sound/items/quaddamage.wav",{"models/powerups/instant/quad.md3","models/powerups/instant/quad_ring.md3",NULL,NULL},"icons/quad","Quad Damage",30,5,1,"","sound/items/damage2.wav sound/items/damage3.wav"},
  {"item_enviro","sound/items/protect.wav",{"models/powerups/instant/enviro.md3","models/powerups/instant/enviro_ring.md3",NULL,NULL},"icons/envirosuit","Battle Suit",30,5,2,"","sound/items/airout.wav sound/items/protect3.wav"},
  {"item_haste","sound/items/haste.wav",{"models/powerups/instant/haste.md3","models/powerups/instant/haste_ring.md3",NULL,NULL},"icons/haste","Speed",30,5,3,"",""},
  {"item_invis","sound/items/invisibility.wav",{"models/powerups/instant/invis.md3","models/powerups/instant/invis_ring.md3",NULL,NULL},"icons/invis","Invisibility",30,5,4,"",""},
  {"item_regen","sound/items/regeneration.wav",{"models/powerups/instant/regen.md3","models/powerups/instant/regen_ring.md3",NULL,NULL},"icons/regen","Regeneration",30,5,5,"","sound/items/regen.wav"},
  {"item_flight","sound/items/flight.wav",{"models/powerups/instant/flight.md3","models/powerups/instant/flight_ring.md3",NULL,NULL},"icons/flight","Flight",60,5,6,"","sound/items/flight.wav"},
  {"team_CTF_redflag",NULL,{"models/flags/r_flag.md3",NULL,NULL,NULL},"icons/iconf_red1","Red Flag",0,8,7,"",""},
  {"team_CTF_blueflag",NULL,{"models/flags/b_flag.md3",NULL,NULL,NULL},"icons/iconf_blu1","Blue Flag",0,8,8,"",""},
  {"holdable_kamikaze","sound/items/holdable.wav",{"models/powerups/kamikazi.md3",NULL,NULL,NULL},"icons/kamikaze","Kamikaze",60,6,3,"","sound/items/kamikazerespawn.wav"},
  {"holdable_portal","sound/items/holdable.wav",{"models/powerups/holdable/porter.md3",NULL,NULL,NULL},"icons/portal","Portal",60,6,4,"",""},
  {"holdable_invulnerability","sound/items/holdable.wav",{"models/powerups/holdable/invulnerability.md3",NULL,NULL,NULL},"icons/invulnerability","Invulnerability",60,6,5,"",""},
  {"ammo_nails","sound/misc/am_pkup.wav",{"models/powerups/ammo/nailgunam.md3",NULL,NULL,NULL},"icons/icona_nailgun","Nails",20,2,11,"",""},
  {"ammo_mines","sound/misc/am_pkup.wav",{"models/powerups/ammo/proxmineam.md3",NULL,NULL,NULL},"icons/icona_proxlauncher","Proximity Mines",10,2,12,"",""},
  {"ammo_belt","sound/misc/am_pkup.wav",{"models/powerups/ammo/chaingunam.md3",NULL,NULL,NULL},"icons/icona_chaingun","Chaingun Belt",100,2,13,"",""},
  {"item_scout","sound/items/scout.wav",{"models/powerups/scout.md3",NULL,NULL,NULL},"icons/scout","Scout",30,7,10,"",""},
  {"item_guard","sound/items/guard.wav",{"models/powerups/guard.md3",NULL,NULL,NULL},"icons/guard","Guard",30,7,11,"",""},
  {"item_doubler","sound/items/doubler.wav",{"models/powerups/doubler.md3",NULL,NULL,NULL},"icons/doubler","Doubler",30,7,12,"",""},
  {"item_ammoregen","sound/items/ammoregen.wav",{"models/powerups/ammo.md3",NULL,NULL,NULL},"icons/ammo_regen","Ammo Regen",30,7,13,"",""},
  {"team_CTF_neutralflag",NULL,{"models/flags/n_flag.md3",NULL,NULL,NULL},"icons/iconf_neutral1","Neutral Flag",0,8,9,"",""},
  {"item_redcube","sound/misc/am_pkup.wav",{"models/powerups/orb/r_orb.md3",NULL,NULL,NULL},"icons/iconh_rorb","Red Cube",0,8,0,"",""},
  {"item_bluecube","sound/misc/am_pkup.wav",{"models/powerups/orb/b_orb.md3",NULL,NULL,NULL},"icons/iconh_borb","Blue Cube",0,8,0,"",""},
  {"weapon_nailgun","sound/misc/w_pkup.wav",{"models/weapons/nailgun/nailgun.md3",NULL,NULL,NULL},"icons/iconw_nailgun","Nailgun",10,1,11,"",""},
  {"weapon_prox_launcher","sound/misc/w_pkup.wav",{"models/weapons/proxmine/proxmine.md3",NULL,NULL,NULL},"icons/iconw_proxlauncher","Prox Launcher",5,1,12,"","sound/weapons/proxmine/wstbtick.wav sound/weapons/proxmine/wstbactv.wav sound/weapons/proxmine/wstbimpl.wav sound/weapons/proxmine/wstbimpm.wav sound/weapons/proxmine/wstbimpd.wav sound/weapons/proxmine/wstbactv.wav"},
  {"weapon_chaingun","sound/misc/w_pkup.wav",{"models/weapons/vulcan/vulcan.md3",NULL,NULL,NULL},"icons/iconw_chaingun","Chaingun",80,1,13,"","sound/weapons/vulcan/wvulwind.wav"},
};

const qa_q3_base_item_definition *qa_q3_base_item_list(qa_q3_base_product product, size_t *out_count) {
  *out_count = product == QA_Q3_BASE_PRODUCT_BASEQ3 ? BASE_ITEM_COUNT : ITEM_COUNT;
  return DEFINITIONS;
}
static size_t list_count(qa_q3_base_product p){ return p==QA_Q3_BASE_PRODUCT_BASEQ3?BASE_ITEM_COUNT:ITEM_COUNT; }
const qa_q3_base_item_definition *qa_q3_base_item_at(qa_q3_base_product product, double index) {
  /* TS array index: non-integer/out-of-range is undefined -> RangeError. */
  if (!(index == trunc(index)) || index < 0 || index >= (double)list_count(product))
    qa_range_error("Item index out of range: %g", index);
  return &DEFINITIONS[(size_t)index];
}
static bool folded_equal(const char *a, const char *b) {
  /* TS fold maps A-Z to a-z over UTF-16 units; bytes here. */
  for (;;) {
    unsigned char ca = (unsigned char)*a, cb = (unsigned char)*b;
    if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + 32);
    if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + 32);
    if (ca != cb) return false;
    if (ca == 0) return true;
    a++; b++;
  }
}
const qa_q3_base_item_definition *qa_q3_base_find_item(qa_q3_base_product product, const char *pickup_name) {
  size_t n = list_count(product);
  for (size_t i = 0; i < n; i++)
    if (DEFINITIONS[i].pickup_name && folded_equal(DEFINITIONS[i].pickup_name, pickup_name)) return &DEFINITIONS[i];
  return NULL;
}
const qa_q3_base_item_definition *qa_q3_base_find_item_for_powerup(qa_q3_base_product product, qa_q3_powerup powerup) {
  size_t n = list_count(product);
  for (size_t i = 0; i < n; i++) {
    double t = DEFINITIONS[i].type;
    if ((t == QA_Q3_BASE_IT_POWERUP || t == QA_Q3_BASE_IT_TEAM || t == QA_Q3_BASE_IT_PERSISTANT_POWERUP)
        && DEFINITIONS[i].tag == (double)powerup) return &DEFINITIONS[i];
  }
  return NULL;
}
const qa_q3_base_item_definition *qa_q3_base_find_item_for_holdable(qa_q3_base_product product, qa_q3_holdable holdable) {
  size_t n = list_count(product);
  for (size_t i = 0; i < n; i++)
    if (DEFINITIONS[i].type == QA_Q3_BASE_IT_HOLDABLE && DEFINITIONS[i].tag == (double)holdable) return &DEFINITIONS[i];
  qa_common_error(QA_COMMON_ERROR_DROP, "HoldableItem not found");
}
const qa_q3_base_item_definition *qa_q3_base_find_item_for_weapon(qa_q3_base_product product, qa_q3_weapon weapon) {
  size_t n = list_count(product);
  for (size_t i = 0; i < n; i++)
    if (DEFINITIONS[i].type == QA_Q3_BASE_IT_WEAPON && DEFINITIONS[i].tag == (double)weapon) return &DEFINITIONS[i];
  char msg[96];
  snprintf(msg, sizeof msg, "Couldn't find item for weapon %d", (int)weapon);
  qa_common_error(QA_COMMON_ERROR_DROP, msg);
}
bool qa_q3_base_can_q3_armor_be_grabbed(const qa_q3_base_player_inventory *ps) {
  if (ps->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    if (qa_q3_base_item_at(ps->product, ps->persistent_powerup_index)->tag == QA_Q3_PW_SCOUT) return false;
    double upper = qa_q3_base_item_at(ps->product, ps->persistent_powerup_index)->tag == QA_Q3_PW_GUARD
      ? ps->max_health : ps->max_health * 2;
    return ps->armor < upper;
  }
  return ps->armor < ps->max_health * 2;
}
static int32_t to_int32(double v) {
  if (!isfinite(v)) return 0;
  double m = fmod(trunc(v), 4294967296.0);
  if (m >= 2147483648.0) m -= 4294967296.0;
  if (m < -2147483648.0) m += 4294967296.0;
  return (int32_t)m;
}
bool qa_q3_base_can_item_be_grabbed(double gametype, qa_q3_base_pickup_entity ent, const qa_q3_base_player_inventory *ps) {
  if (ent.model_index < 1 || ent.model_index >= (double)list_count(ps->product))
    qa_common_error(QA_COMMON_ERROR_DROP, "BG_CanItemBeGrabbed: index out of range");
  const qa_q3_base_item_definition *item = qa_q3_base_item_at(ps->product, ent.model_index);
  if (item->type == QA_Q3_BASE_IT_WEAPON) return true;
  if (item->type == QA_Q3_BASE_IT_AMMO) return ps->ammo(ps->ctx, (qa_q3_weapon)item->tag) < 200;
  if (item->type == QA_Q3_BASE_IT_ARMOR) return qa_q3_base_can_q3_armor_be_grabbed(ps);
  if (item->type == QA_Q3_BASE_IT_HEALTH) {
    if (ps->product == QA_Q3_BASE_PRODUCT_MISSIONPACK
        && qa_q3_base_item_at(ps->product, ps->persistent_powerup_index)->tag == QA_Q3_PW_GUARD)
      return ps->health < ps->max_health;
    return ps->health < ps->max_health * (item->quantity == 5 || item->quantity == 100 ? 2 : 1);
  }
  if (item->type == QA_Q3_BASE_IT_POWERUP) return true;
  if (item->type == QA_Q3_BASE_IT_PERSISTANT_POWERUP) {
    if (ps->product == QA_Q3_BASE_PRODUCT_BASEQ3 || ps->persistent_powerup_index != 0) return false;
    if ((to_int32(ent.generic1) & 2) != 0 && ps->team != QA_Q3_BASE_TEAM_RED) return false;
    if ((to_int32(ent.generic1) & 4) != 0 && ps->team != QA_Q3_BASE_TEAM_BLUE) return false;
    return true;
  }
  if (item->type == QA_Q3_BASE_IT_TEAM) {
    if (ps->product == QA_Q3_BASE_PRODUCT_MISSIONPACK && gametype == QA_Q3_BASE_GT_1FCTF) {
      if (item->tag == QA_Q3_PW_NEUTRALFLAG) return true;
      if (ps->team == QA_Q3_BASE_TEAM_RED && item->tag == QA_Q3_PW_BLUEFLAG
          && ps->powerup(ps->ctx, QA_Q3_PW_NEUTRALFLAG) != 0) return true;
      if (ps->team == QA_Q3_BASE_TEAM_BLUE && item->tag == QA_Q3_PW_REDFLAG
          && ps->powerup(ps->ctx, QA_Q3_PW_NEUTRALFLAG) != 0) return true;
    }
    if (gametype == QA_Q3_BASE_GT_CTF) {
      if (ps->team == QA_Q3_BASE_TEAM_RED)
        return item->tag == QA_Q3_PW_BLUEFLAG
          || (item->tag == QA_Q3_PW_REDFLAG && (ent.model_index2 != 0 || ps->powerup(ps->ctx, QA_Q3_PW_BLUEFLAG) != 0));
      if (ps->team == QA_Q3_BASE_TEAM_BLUE)
        return item->tag == QA_Q3_PW_REDFLAG
          || (item->tag == QA_Q3_PW_BLUEFLAG && (ent.model_index2 != 0 || ps->powerup(ps->ctx, QA_Q3_PW_REDFLAG) != 0));
    }
    return ps->product == QA_Q3_BASE_PRODUCT_MISSIONPACK && gametype == QA_Q3_BASE_GT_HARVESTER;
  }
  if (item->type == QA_Q3_BASE_IT_HOLDABLE) return ps->holdable_item == 0;
  if (item->type == QA_Q3_BASE_IT_BAD)
    qa_common_error(QA_COMMON_ERROR_DROP, "BG_CanItemBeGrabbed: IT_BAD");
  return false; /* TS switch has no default: falls off returning undefined. */
}
bool qa_q3_base_player_touches_item(qa_vec3 player_origin, qa_q3_base_trajectory item_position, double at_time) {
  qa_vec3 origin = qa_q3_base_evaluate_trajectory(item_position, at_time);
  double x = (double)(float)(player_origin.x - origin.x);
  double y = (double)(float)(player_origin.y - origin.y);
  double z = (double)(float)(player_origin.z - origin.z);
  return !(x > 44 || x < -50 || y > 36 || y < -36 || z > 36 || z < -36);
}
