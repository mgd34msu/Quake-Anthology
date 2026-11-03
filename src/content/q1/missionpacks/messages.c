/* Port of src/content/q1/missionpacks/messages.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/messages.h"

#include "missionpacks_internal.h"

static const struct { const char *key; const char *value; } CLASSIC[] = {
  {"$qc_wetsuit","Wetsuit"},{"$qc_empathy_shields","Empathy Shields"},{"$qc_mjolnir","Mjolnir"},{"$qc_laser_cannon","Laser Cannon"},{"$qc_prox_gun","Proximity Gun"},
  {"$qc_power_shield","Power Shield"},{"$qc_anti_grav_belt","Anti-Grav Belt"},{"$qc_vengeance_sphere","Vengeance Sphere"},{"$qc_lava_nails","lava nails"},{"$qc_multi_rockets","multi rockets"},
  {"$qc_quad_damage","Quad Damage"},{"$qc_pentagram_of_protection","Pentagram of Protection"},{"$qc_ring_of_shadows","Ring of Shadows"},
  {"$qc_wetsuit_fade","Air supply in Wetsuit is running out\n"},{"$qc_empathy_fade","Empathy Shields are running out\n"},
  {"$qc_shield_failing","Shield failing...\n"},{"$qc_shield_lost","Shield Lost.\n"},{"$qc_antigrav_failing","Antigrav failing...\n"},{"$qc_antigrav_lost","Antigrav Lost.\n"},
  {"$qc_vengeance_lost","Vengeance Sphere Lost\n"},{"$qc_you_are_denied_vengeance","You are denied Vengeance"},
  {"$qc_lava_enabled","Lava Enabled\n"},{"$qc_super_lava_enabled","Super Lava Enabled\n"},{"$qc_multi_gl_enabled","Multi Grenades Enabled\n"},{"$qc_multi_rl_enabled","Multi Rockets Enabled\n"},{"$qc_plasma_enabled","Plasma Gun Enabled\n"},
  {"$qc_no_weapon","no weapon.\n"},{"$qc_not_enough_ammo","not enough ammo.\n"},{"$qc_got_horn","You got the Horn of Conjuring\n"},
  {"$qc_normal_nails","Normal Nails\n"},{"$qc_normal_grenades","Normal Grenades\n"},{"$qc_normal_rockets","Normal Rockets\n"},{"$qc_lightning_gun","Lightning Gun\n"},
  {"$qc_multi_gl","Multi Grenades\n"},{"$qc_multi_rl","Multi Rockets\n"},{"$qc_plasma_gun","Plasma Gun\n"},
  {"$qc_no_ammo_available","No ammo available!\n"},{"$qc_quad_cheat","quad cheat\n"},{"$qc_wetsuit_cheat","wetsuit cheat\n"},{"$qc_empathy_cheat","empathy shields cheat\n"},{"$qc_genocide_cheat","Genocide!\n"},{"$qc_dump_player_loc","Dumping Player Location\n"},
  {"$qc_double_shotgun","Double-barrelled Shotgun"},{"$qc_nailgun","Nailgun"},{"$qc_super_nailgun","Super Nailgun"},{"$qc_grenade_launcher","Grenade Launcher"},{"$qc_rocket_launcher","Rocket Launcher"},{"$qc_thunderbolt","Thunderbolt"},
};
const char *qa_q1mp_mission_classic(const char *key) {
  for (size_t i = 0; i < sizeof CLASSIC / sizeof CLASSIC[0]; i++)
    if (qa_q1mp_streq(CLASSIC[i].key, key)) return CLASSIC[i].value;
  return NULL;
}
void qa_q1mp_mission_message(qa_q1_services *game, const qa_actor_id *player, const char *key) {
  const char *text = key;
  if (qa_q1mp_streq(game->options.edition, "classic")) {
    const char *classic = qa_q1mp_mission_classic(key);
    text = classic != NULL ? classic : key;
  }
  qa_q1_message(game, player, text, false, NULL, 0);
}
void qa_q1mp_mission_pickup_message(qa_q1_services *game, qa_actor_id player, const char *key) {
  if (qa_q1mp_streq(game->options.edition, "classic")) {
    char text[512];
    const char *classic = qa_q1mp_mission_classic(key);
    (void)snprintf(text, sizeof text, "You got the %s\n", classic != NULL ? classic : key);
    qa_q1_message(game, &player, text, false, NULL, 0);
    return;
  }
  const qa_q1_msg_arg args[1] = {{false, 0, key}};
  qa_q1_message(game, &player, "$qc_got_item", false, args, 1);
}
