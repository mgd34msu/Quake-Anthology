/* Port of src/content/q1/missionpacks/obituaries.ts over the base
 * q1Obituary (qa_q1_obituary_compute) and classic texts
 * (qa/content/q1/base/messages.h). GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/obituaries.h"

#include "missionpacks_internal.h"
#include "qa/content/q1/base/messages.h"
#include "qa/foundation/js.h"

static const struct { const char *key; const char *value; } CLASSIC[] = {
  {"$qc_death_empathy1","{0} shares {1}'s pain\n"},{"$qc_death_empathy2","{0} feels {1}'s pain\n"},
  {"$qc_death_bomb1","{0} got too friendly with {1}'s bomb\n"},{"$qc_death_bomb2","{0} did the rhumba with {1}'s bomb\n"},
  {"$qc_death_laser1","{0} was toasted by {1}'s laser\n"},{"$qc_death_laser2","{0} was radiated by {1}'s laser\n"},{"$qc_death_hammer","{0} was slammed by {1}'s hammer\n"},
  {"$qc_death_grappled","{0} was grappled by {1}\n"},{"$qc_death_burned","{0} was burned by {1}\n"},{"$qc_death_fused","{0} was fused by {1}\n"},{"$qc_death_blasted","{0} was blasted to bits by {1}\n"},
  {"$qc_death_vengeance","{0} was purged by the Vengeance Sphere\n"},{"$qc_death_smashed","{0} was smashed by {1}\n"},
  {"$qc_changed_teams","{0} changed teams\n"},{"$qc_tried_change_teams","{0} tried to change teams\n"},{"$qc_suicide_loaded","{0} checks if his weapon is loaded\n"},
  {"$qc_ks_dragon1","{0} was annihilated by the Dragon\n"},{"$qc_ks_dragon2","{0} was squashed by the Dragon\n"},{"$qc_ks_eel","{0} was electrified by an Eel\n"},
  {"$qc_ks_wrath","{0} was disintegrated by a Wrath\n"},{"$qc_ks_overlord","{0} was obliterated by an Overlord\n"},{"$qc_ks_swordsman","{0} was slit open by a Phantom Swordsman\n"},
  {"$qc_ks_hephaestus","{0} fries in Hephaestus' fury\n"},{"$qc_ks_guardian","{0} was crushed by a Guardian\n"},{"$qc_ks_mummy","{0} was Mummified\n"},
  {"$qc_ks_gremlin","{0} was outsmarted by a Gremlin\n"},{"$qc_ks_centroid","{0} was stung by a Centroid\n"},{"$qc_ks_armagon","{0} was outgunned by Armagon\n"},
  {"$qc_ks_blew_up","{0} blew up\n"},{"$qc_ks_spiked","{0} was spiked\n"},{"$qc_ks_lavaball","{0} ate a lavaball\n"},{"$qc_ks_tried_leave","{0} tried to leave\n"},
  {"$qc_ks_rode_lightning","{0} rode the lightning\n"},{"$qc_ks_cleaved","{0} was cleaved in two\n"},{"$qc_ks_sliced","{0} was sliced to pieces\n"},{"$qc_ks_plasma","{0} was turned to plasma\n"},
};
static const struct { const char *key; const char *value; } MONSTERS[] = {
  {"monster_army","$qc_ks_grunt"},{"monster_demon1","$qc_ks_fiend"},{"monster_dog","$qc_ks_rottweiler"},{"monster_dragon","$qc_ks_dragon"},{"monster_dragon_dead","$qc_ks_dragon2"},
  {"monster_enforcer","$qc_ks_enforcer"},{"monster_fish","$qc_ks_rotfish"},{"monster_hell_knight","$qc_ks_deathknight"},{"monster_knight","$qc_ks_knight"},{"monster_ogre","$qc_ks_ogre"},
  {"monster_oldone","$qc_ks_shub"},{"monster_shalrath","$qc_ks_vore"},{"monster_shambler","$qc_ks_shambler"},{"monster_tarbaby","$qc_ks_spawn"},{"monster_vomit","$qc_ks_vomitus"},{"monster_wizard","$qc_ks_scrag"},{"monster_zombie","$qc_ks_zombie"},
  {"monster_gremlin","$qc_ks_gremlin"},{"monster_scourge","$qc_ks_centroid"},{"monster_armagon","$qc_ks_armagon"},{"monster_eel","$qc_ks_eel"},{"monster_wrath","$qc_ks_wrath"},{"monster_super_wrath","$qc_ks_overlord"},
  {"monster_sword","$qc_ks_swordsman"},{"monster_lava_man","$qc_ks_hephaestus"},{"monster_morph","$qc_ks_guardian"},{"monster_mummy","$qc_ks_mummy"},
};
static const char *lookup(const void *table, size_t n, const char *key) {
  const struct { const char *key; const char *value; } *t = table;
  for (size_t i = 0; i < n; i++) if (qa_q1mp_streq(t[i].key, key)) return t[i].value;
  return NULL;
}
static void classic_text(const char *key, const char *const *args, size_t nargs, char *out, size_t size) {
  const char *tpl = lookup(CLASSIC, sizeof CLASSIC / sizeof CLASSIC[0], key);
  if (!tpl) { qa_q1_classic_obituary_text(key, args, nargs, out, size); return; }
  size_t o = 0;
  for (size_t i = 0; tpl[i] && o + 1 < size; i++) {
    if (tpl[i] == '{') {
      /* /\{([0-9]+)\}/u: one or more digits only; strtol also took "{}", a sign and white space. */
      const char *end = tpl + i + 1;
      while (*end >= '0' && *end <= '9') end++;
      double idx = qa_js_parse_int(tpl + i + 1, 10); /* Number(index) of the digit run */
      if (end != tpl + i + 1 && *end == '}') {
        const char *a = (idx < (double)nargs && args[(size_t)idx]) ? args[(size_t)idx] : "";
        size_t len = strlen(a); if (len > size - o - 1) len = size - o - 1;
        memcpy(out + o, a, len); o += len; i = (size_t)(end - tpl); continue;
      }
    }
    out[o++] = tpl[i];
  }
  out[o] = '\0';
}
/* The fallback replays firstRoll through a wrapped random. */
typedef struct replay_random { const qa_q1_obituary_input *input; double first; bool replay; } replay_random;
static double replayed(void *ctx) {
  replay_random *state = ctx;
  if (state->replay) { state->replay = false; return state->first; }
  return state->input->random(state->input->random_ctx);
}
/* TS fallback(override): attacker_null applies { attacker: null }. replay is
 * shared by every fallback of one call, as the TS closure variable is. */
static qa_q1_obituary fallback(const qa_q1_obituary_input *input, replay_random *replay, bool attacker_null) {
  qa_q1_obituary_input copy = *input;
  if (attacker_null) copy.attacker = NULL;
  copy.random_ctx = replay; copy.random = replayed;
  return qa_q1_obituary_compute(&copy);
}
static qa_q1_obituary result(const qa_q1_obituary_input *input, const char *key, const qa_actor_id *actor, double delta,
                             const char *const *args, size_t arg_count, const char *classic_override) {
  qa_q1_obituary out;
  memset(&out, 0, sizeof out);
  if (key[0] != '\0') {
    out.has_message = true;
    if (qa_q1mp_streq(input->edition, "rerelease")) {
      (void)snprintf(out.text, sizeof out.text, "%s", key);
      for (size_t i = 0; i < arg_count; i++) out.arguments[i] = args[i];
      out.argument_count = arg_count;
    } else if (classic_override != NULL) (void)snprintf(out.text, sizeof out.text, "%s", classic_override);
    else classic_text(key, args, arg_count, out.text, sizeof out.text);
  }
  if (actor != NULL) { out.has_score = true; out.score_actor = *actor; out.score_delta = delta; }
  return out;
}
qa_q1_obituary qa_q1mp_mission_pack_obituary(const qa_q1_obituary_input *input, const qa_q1mp_obituary_context *context) {
  const qa_q1_obituary_actor *victim = &input->victim, *attacker = input->attacker;
  replay_random replay = {input, input->random(input->random_ctx), true};
  const double first_roll = replay.first;
  const char *const victim_args[1] = {victim->name};
  const bool hipnotic = qa_q1mp_streq(context->pack, "hipnotic"), rogue = qa_q1mp_streq(context->pack, "rogue");
  if (!victim->is_player || (attacker != NULL && (qa_q1mp_streq(attacker->classname, "teledeath") || qa_q1mp_streq(attacker->classname, "teledeath2"))))
    return fallback(input, &replay, false);
  if (attacker != NULL && attacker->is_player) {
    if (qa_same_actor(victim->actor, attacker->actor)) {
      if ((qa_q1mp_streq(victim->weapon, "lightning") && victim->water_level > 1) || qa_q1mp_streq(victim->weapon, "grenadelauncher"))
        return fallback(input, &replay, false);
      if (hipnotic)
        return result(input, qa_q1mp_streq(input->edition, "classic") ? (first_roll > 0.4 ? "$qc_suicide_bored" : "$qc_suicide_loaded")
                                                                      : (first_roll != 0 ? "$qc_suicide_bored" : "$qc_suicide_loaded"),
                      &victim->actor, -1, victim_args, 1, NULL);
      if (qa_q1mp_streq(input->edition, "rerelease") && first_roll < 0.5) return result(input, "$qc_suicide_bored", &victim->actor, -1, victim_args, 1, NULL);
      if (input->teamplay != 0 && victim->team != context->victim_saved_team)
        return result(input, (qa_js_to_int32(context->gamecfg) & 16) != 0 ? "$qc_changed_teams" : "$qc_tried_change_teams",
                      &victim->actor, -1, victim_args, 1, NULL);
      return result(input, qa_q1mp_streq(input->edition, "classic") ? "$qc_suicide_bored" : "$qc_suicide_loaded", &victim->actor, -1, victim_args, 1, NULL);
    }
    if (input->teamplay == 2 && victim->team > 0 && victim->team == attacker->team) return fallback(input, &replay, false);
    const double points = rogue && input->teamplay == 3 ? (context->tag_score != NULL ? context->tag_score(context->tag_ctx) : 1) : 1;
    const char *const pair[2] = {victim->name, attacker->name};
    if (hipnotic) {
      if (qa_q1mp_streq(input->death_type, "hipnotic:empathy"))
        return result(input, input->random(input->random_ctx) < 0.5 ? "$qc_death_empathy1" : "$qc_death_empathy2", &attacker->actor, points, pair, 2, NULL);
      if (qa_q1mp_streq(context->inflictor_classname, "proximity_grenade"))
        return result(input, input->random(input->random_ctx) < 0.5 ? "$qc_death_bomb1" : "$qc_death_bomb2", &attacker->actor, points, pair, 2, NULL);
      if (qa_q1mp_streq(attacker->weapon, "hipnotic:laser"))
        return result(input, input->random(input->random_ctx) < 0.5 ? "$qc_death_laser1" : "$qc_death_laser2", &attacker->actor, points, pair, 2, NULL);
      if (qa_q1mp_streq(attacker->weapon, "hipnotic:mjolnir")) return result(input, "$qc_death_hammer", &attacker->actor, points, pair, 2, NULL);
    } else {
      if (qa_q1mp_streq(attacker->weapon, "rogue:grapple")) return result(input, "$qc_death_grappled", &attacker->actor, points, pair, 2, NULL);
      if (qa_q1mp_streq(attacker->weapon, "rogue:lava-nailgun") || qa_q1mp_streq(attacker->weapon, "rogue:lava-supernailgun"))
        return result(input, "$qc_death_burned", &attacker->actor, points, pair, 2, NULL);
      if (qa_q1mp_streq(attacker->weapon, "rogue:plasma")) return result(input, "$qc_death_fused", &attacker->actor, points, pair, 2, NULL);
      if (qa_q1mp_streq(attacker->weapon, "rogue:multi-grenade") || qa_q1mp_streq(attacker->weapon, "rogue:multi-rocket"))
        return result(input, "$qc_death_blasted", &attacker->actor, points, pair, 2, NULL);
    }
    qa_q1_obituary base = fallback(input, &replay, false);
    base.has_score = true; base.score_actor = attacker->actor; base.score_delta = points;
    return base;
  }
  if (hipnotic) {
    if (context->attacker_death_type[0] != '\0') {
      char text[512];
      (void)snprintf(text, sizeof text, "%s %s\n", victim->name, context->attacker_death_type);
      return result(input, context->attacker_death_type, &victim->actor, -1, victim_args, 1, text);
    }
    if (!qa_q1mp_streq(victim->water_type, "empty")) return fallback(input, &replay, true);
  }
  if (attacker != NULL && attacker->is_monster) {
    const char *mapped = lookup(MONSTERS, sizeof MONSTERS / sizeof MONSTERS[0], attacker->classname);
    const char *key = rogue && qa_q1mp_streq(attacker->classname, "monster_dragon") ? "$qc_ks_dragon1" : mapped != NULL ? mapped : "";
    const char *old = qa_q1_classic_monster_obituary(attacker->classname);
    char text[512];
    const char *classic_override = NULL;
    if (lookup(CLASSIC, sizeof CLASSIC / sizeof CLASSIC[0], key) == NULL && old != NULL) {
      (void)snprintf(text, sizeof text, "%s%s", victim->name, old);
      classic_override = text;
    }
    return result(input, key, &victim->actor, -1, victim_args, 1, classic_override);
  }
  if (attacker != NULL && qa_q1mp_streq(attacker->classname, "explo_box")) return result(input, "$qc_ks_blew_up", &victim->actor, -1, victim_args, 1, NULL);
  if (attacker != NULL && attacker->brush && !qa_q1mp_streq(attacker->classname, "worldspawn"))
    return result(input, "$qc_death_squish", &victim->actor, -1, victim_args, 1, NULL);
  if (hipnotic && qa_q1mp_streq(input->death_type, "falling")) return result(input, "$qc_death_fall", &victim->actor, -1, victim_args, 1, NULL);
  const char *trap = attacker != NULL ? attacker->classname : NULL;
  if (qa_q1mp_streq(trap, "trap_shooter") || qa_q1mp_streq(trap, "trap_spikeshooter")) return result(input, "$qc_ks_spiked", &victim->actor, -1, victim_args, 1, NULL);
  if (qa_q1mp_streq(trap, "fireball")) return result(input, "$qc_ks_lavaball", &victim->actor, -1, victim_args, 1, NULL);
  if (qa_q1mp_streq(trap, "trigger_changelevel")) return result(input, "$qc_ks_tried_leave", &victim->actor, -1, victim_args, 1, NULL);
  if (rogue) {
    if (qa_q1mp_streq(trap, "ltrail_start") || qa_q1mp_streq(trap, "ltrail_relay")) return result(input, "$qc_ks_rode_lightning", &victim->actor, -1, victim_args, 1, NULL);
    if (qa_q1mp_streq(trap, "pendulum") || qa_q1mp_streq(trap, "buzzsaw") || qa_q1mp_streq(trap, "plasma"))
      return result(input, qa_q1mp_streq(trap, "pendulum") ? "$qc_ks_cleaved" : qa_q1mp_streq(trap, "buzzsaw") ? "$qc_ks_sliced" : "$qc_ks_plasma",
                    &victim->actor, -1, victim_args, 1, NULL);
    if (qa_q1mp_streq(trap, "Vengeance")) return result(input, "$qc_death_vengeance", NULL, 0, victim_args, 1, NULL);
    if (qa_q1mp_streq(trap, "power_shield") && input->telefrag_owner != NULL) {
      const char *const smashed[2] = {victim->name, input->telefrag_owner->name};
      return result(input, "$qc_death_smashed", &input->telefrag_owner->actor, 1, smashed, 2, NULL);
    }
  }
  return fallback(input, &replay, false);
}
