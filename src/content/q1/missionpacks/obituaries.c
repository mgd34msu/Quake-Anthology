/* Port of src/content/q1/missionpacks/obituaries.ts. Classic text via
 * landed qa/content/q1/base/messages.h. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/obituaries.h"
#include "missionpacks_internal.h"
#include "qa/content/q1/base/messages.h"
#include "qa/content/q1/base/rules.h"

/* TS q1Obituary (src/content/q1/base/rules.ts) via the landed
 * qa_q1_obituary_compute. The mission person mirror carries no
 * health/quad/invulnerable/killString, so those read 0/"". */
static qa_q1_obituary_actor obituary_actor(const qa_q1mp_obituary_person *person) {
  qa_q1_obituary_actor actor = {0};
  actor.actor = person->actor;
  actor.name = person->name != NULL ? person->name : "";
  actor.classname = person->classname != NULL ? person->classname : "";
  actor.is_player = person->is_player;
  actor.is_monster = person->is_monster;
  actor.team = person->team;
  actor.water_type = person->water_type != NULL ? person->water_type : "";
  actor.water_level = person->water_level;
  actor.weapon = person->weapon;
  actor.brush = person->brush;
  actor.kill_string = "";
  return actor;
}
qa_q1mp_obituary qa_q1mp_ext_q1_obituary(const qa_q1mp_obituary_input *input,
                                         bool has_attacker_override, bool attacker_null) {
  bool has_attacker = has_attacker_override ? !attacker_null : input->has_attacker;
  qa_q1_obituary_actor victim = obituary_actor(&input->victim);
  qa_q1_obituary_actor attacker = obituary_actor(&input->attacker);
  qa_q1_obituary_actor telefrag_owner = obituary_actor(&input->telefrag_owner);
  qa_q1_obituary_input base = {0};
  base.edition = input->edition;
  base.victim = victim;
  base.attacker = has_attacker ? &attacker : NULL;
  base.telefrag_owner = input->has_telefrag_owner ? &telefrag_owner : NULL;
  base.teamplay = input->teamplay;
  base.death_type = input->death_type;
  base.random_ctx = input->random_ctx;
  base.random = input->random;
  qa_q1_obituary result = qa_q1_obituary_compute(&base);
  qa_q1mp_obituary out = {0};
  out.has_message = result.has_message;
  (void)snprintf(out.text, sizeof out.text, "%s", result.text);
  for (size_t i = 0; i < result.argument_count && i < 4; i++) out.args[i] = result.arguments[i];
  out.arg_count = result.argument_count < 4 ? result.argument_count : 4;
  out.has_score = result.has_score;
  out.score_actor = result.score_actor;
  out.score_delta = result.score_delta;
  out.achievement = result.has_achievement ? result.achievement_id : NULL;
  return out;
}
#include <stdio.h>
#include <string.h>

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
      char *end = NULL; long idx = strtol(tpl + i + 1, &end, 10);
      if (end && *end == '}') {
        const char *a = (idx >= 0 && (size_t)idx < nargs && args[idx]) ? args[idx] : "";
        size_t len = strlen(a); if (len > size - o - 1) len = size - o - 1;
        memcpy(out + o, a, len); o += len; i = (size_t)(end - tpl); continue;
      }
    }
    out[o++] = tpl[i];
  }
  out[o] = '\0';
}
#include <stdlib.h>
typedef struct fb { const qa_q1mp_obituary_input *input; double first; bool replay; } fb;
/* The ext fallback replays firstRoll through a wrapped input. */
static double fb_random(void *ctx) { fb *s = ctx; if (s->replay) { s->replay = false; return s->first; } return s->input->random(s->input->random_ctx); }
static qa_q1mp_obituary fallback(const qa_q1mp_obituary_input *input, double first, bool attacker_null) {
  qa_q1mp_obituary_input copy = *input; fb state = {input, first, true};
  copy.random_ctx = &state; copy.random = fb_random;
  if (attacker_null) copy.has_attacker = false;
  return qa_q1mp_ext_q1_obituary(&copy, attacker_null, attacker_null);
}
static qa_q1mp_obituary make_result(const qa_q1mp_obituary_input *input, const char *key, bool has_actor, qa_actor_id actor, double delta, const char *const *args, size_t nargs, const char *classic_override) {
  qa_q1mp_obituary out = {0};
  if (key && key[0]) {
    out.has_message = true;
    if (qa_q1mp_streq(input->edition, "rerelease")) {
      (void)snprintf(out.text, sizeof out.text, "%s", key);
      for (size_t i = 0; i < nargs && i < 4; i++) out.args[i] = args[i];
      out.arg_count = nargs < 4 ? nargs : 4;
    } else if (classic_override) (void)snprintf(out.text, sizeof out.text, "%s", classic_override);
    else classic_text(key, args, nargs, out.text, sizeof out.text);
  }
  out.has_score = has_actor; out.score_actor = actor; out.score_delta = delta; out.achievement = NULL;
  return out;
}
qa_q1mp_obituary qa_q1mp_mission_pack_obituary(const qa_q1mp_obituary_input *input, const qa_q1mp_obituary_context *context) {
  const qa_q1mp_obituary_person *victim = &input->victim;
  const qa_q1mp_obituary_person *attacker = input->has_attacker ? &input->attacker : NULL;
  double first = input->random(input->random_ctx);
  const char *vargs[] = {victim->name};
  if (!victim->is_player || (attacker && (qa_q1mp_streq(attacker->classname,"teledeath")||qa_q1mp_streq(attacker->classname,"teledeath2")))) return fallback(input, first, false);
  if (attacker && attacker->is_player) {
    if (qa_same_actor(victim->actor, attacker->actor)) {
      if ((qa_q1mp_streq(victim->weapon,"lightning") && victim->water_level > 1) || qa_q1mp_streq(victim->weapon,"grenadelauncher")) return fallback(input, first, false);
      if (qa_q1mp_streq(context->pack, "hipnotic"))
        return make_result(input, qa_q1mp_streq(input->edition,"classic") ? (first > 0.4 ? "$qc_suicide_bored" : "$qc_suicide_loaded") : (first != 0 ? "$qc_suicide_bored" : "$qc_suicide_loaded"), true, victim->actor, -1, vargs, 1, NULL);
      if (qa_q1mp_streq(input->edition, "rerelease") && first < 0.5) return make_result(input, "$qc_suicide_bored", true, victim->actor, -1, vargs, 1, NULL);
      if (input->teamplay != 0 && victim->team != context->victim_saved_team)
        return make_result(input, ((int)context->gamecfg & 16) != 0 ? "$qc_changed_teams" : "$qc_tried_change_teams", true, victim->actor, -1, vargs, 1, NULL);
      return make_result(input, qa_q1mp_streq(input->edition,"classic") ? "$qc_suicide_bored" : "$qc_suicide_loaded", true, victim->actor, -1, vargs, 1, NULL);
    }
    if (input->teamplay == 2 && victim->team > 0 && victim->team == attacker->team) return fallback(input, first, false);
    double points = (qa_q1mp_streq(context->pack,"rogue") && input->teamplay == 3) ? (context->tag_score ? context->tag_score(context->tag_ctx) : 1) : 1;
    const char *aargs[] = {victim->name, attacker->name};
    if (qa_q1mp_streq(context->pack, "hipnotic")) {
      if (qa_q1mp_streq(input->death_type, "hipnotic:empathy")) return make_result(input, input->random(input->random_ctx) < 0.5 ? "$qc_death_empathy1" : "$qc_death_empathy2", true, attacker->actor, points, aargs, 2, NULL);
      if (qa_q1mp_streq(context->inflictor_classname, "proximity_grenade")) return make_result(input, input->random(input->random_ctx) < 0.5 ? "$qc_death_bomb1" : "$qc_death_bomb2", true, attacker->actor, points, aargs, 2, NULL);
      if (qa_q1mp_streq(attacker->weapon, "hipnotic:laser")) return make_result(input, input->random(input->random_ctx) < 0.5 ? "$qc_death_laser1" : "$qc_death_laser2", true, attacker->actor, points, aargs, 2, NULL);
      if (qa_q1mp_streq(attacker->weapon, "hipnotic:mjolnir")) return make_result(input, "$qc_death_hammer", true, attacker->actor, points, aargs, 2, NULL);
    } else {
      if (qa_q1mp_streq(attacker->weapon, "rogue:grapple")) return make_result(input, "$qc_death_grappled", true, attacker->actor, points, aargs, 2, NULL);
      if (qa_q1mp_streq(attacker->weapon,"rogue:lava-nailgun")||qa_q1mp_streq(attacker->weapon,"rogue:lava-supernailgun")) return make_result(input, "$qc_death_burned", true, attacker->actor, points, aargs, 2, NULL);
      if (qa_q1mp_streq(attacker->weapon, "rogue:plasma")) return make_result(input, "$qc_death_fused", true, attacker->actor, points, aargs, 2, NULL);
      if (qa_q1mp_streq(attacker->weapon,"rogue:multi-grenade")||qa_q1mp_streq(attacker->weapon,"rogue:multi-rocket")) return make_result(input, "$qc_death_blasted", true, attacker->actor, points, aargs, 2, NULL);
    }
    qa_q1mp_obituary base = fallback(input, first, false);
    base.has_score = true; base.score_actor = attacker->actor; base.score_delta = points; return base;
  }
  if (qa_q1mp_streq(context->pack, "hipnotic")) {
    if (context->attacker_death_type && context->attacker_death_type[0]) {
      char over[512]; (void)snprintf(over, sizeof over, "%s %s\n", victim->name, context->attacker_death_type);
      return make_result(input, context->attacker_death_type, true, victim->actor, -1, vargs, 1, over);
    }
    if (!qa_q1mp_streq(victim->water_type, "empty")) return fallback(input, first, true);
  }
  if (attacker && attacker->is_monster) {
    const char *key = (qa_q1mp_streq(context->pack,"rogue") && qa_q1mp_streq(attacker->classname,"monster_dragon")) ? "$qc_ks_dragon1" : lookup(MONSTERS, sizeof MONSTERS / sizeof MONSTERS[0], attacker->classname);
    if (!key) key = "";
    const char *old = qa_q1_classic_monster_obituary(attacker->classname);
    char over[512]; const char *override_p = NULL;
    if (!lookup(CLASSIC, sizeof CLASSIC / sizeof CLASSIC[0], key) && old) { (void)snprintf(over, sizeof over, "%s%s", victim->name, old); override_p = over; }
    return make_result(input, key, true, victim->actor, -1, vargs, 1, override_p);
  }
  if (attacker && qa_q1mp_streq(attacker->classname, "explo_box")) return make_result(input, "$qc_ks_blew_up", true, victim->actor, -1, vargs, 1, NULL);
  if (attacker && attacker->brush && !qa_q1mp_streq(attacker->classname, "worldspawn")) return make_result(input, "$qc_death_squish", true, victim->actor, -1, vargs, 1, NULL);
  if (qa_q1mp_streq(context->pack,"hipnotic") && qa_q1mp_streq(input->death_type,"falling")) return make_result(input, "$qc_death_fall", true, victim->actor, -1, vargs, 1, NULL);
  const char *trap = attacker ? attacker->classname : NULL;
  if (qa_q1mp_streq(trap,"trap_shooter")||qa_q1mp_streq(trap,"trap_spikeshooter")) return make_result(input, "$qc_ks_spiked", true, victim->actor, -1, vargs, 1, NULL);
  if (qa_q1mp_streq(trap,"fireball")) return make_result(input, "$qc_ks_lavaball", true, victim->actor, -1, vargs, 1, NULL);
  if (qa_q1mp_streq(trap,"trigger_changelevel")) return make_result(input, "$qc_ks_tried_leave", true, victim->actor, -1, vargs, 1, NULL);
  if (qa_q1mp_streq(context->pack, "rogue")) {
    if (qa_q1mp_streq(trap,"ltrail_start")||qa_q1mp_streq(trap,"ltrail_relay")) return make_result(input, "$qc_ks_rode_lightning", true, victim->actor, -1, vargs, 1, NULL);
    if (qa_q1mp_streq(trap,"pendulum")||qa_q1mp_streq(trap,"buzzsaw")||qa_q1mp_streq(trap,"plasma"))
      return make_result(input, qa_q1mp_streq(trap,"pendulum")?"$qc_ks_cleaved":qa_q1mp_streq(trap,"buzzsaw")?"$qc_ks_sliced":"$qc_ks_plasma", true, victim->actor, -1, vargs, 1, NULL);
    if (qa_q1mp_streq(trap,"Vengeance")) return make_result(input, "$qc_death_vengeance", false, victim->actor, 0, vargs, 1, NULL);
    if (qa_q1mp_streq(trap,"power_shield") && input->has_telefrag_owner) {
      const char *targs[] = {victim->name, input->telefrag_owner.name};
      return make_result(input, "$qc_death_smashed", true, input->telefrag_owner.actor, 1, targs, 2, NULL);
    }
  }
  return fallback(input, first, false);
}
