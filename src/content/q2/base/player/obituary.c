/* Port of src/content/q2/base/player/obituary.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/player/obituary.h"
#include "qa/foundation/js.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *environment_message(int64_t mod) {
  switch (mod) {
    case 23: return "suicides";
    case 22: return "cratered";
    case 20: return "was squished";
    case 17: return "sank like a rock";
    case 18: return "melted";
    case 19: return "does a back flip into the lava";
    case 25: case 26: return "blew up";
    case 28: return "found a way out";
    case 30: return "saw the light";
    case 33: return "got blasted";
    case 27: case 29: case 31: return "was in the wrong place";
    default: return NULL;
  }
}

static bool kill_message(int64_t mod, const char **verb, const char **suffix) {
  switch (mod) {
    case 1: *verb = "was blasted by"; *suffix = ""; return true;
    case 2: *verb = "was gunned down by"; *suffix = ""; return true;
    case 3: *verb = "was blown away by"; *suffix = "'s super shotgun"; return true;
    case 4: *verb = "was machinegunned by"; *suffix = ""; return true;
    case 5: *verb = "was cut in half by"; *suffix = "'s chaingun"; return true;
    case 6: *verb = "was popped by"; *suffix = "'s grenade"; return true;
    case 7: *verb = "was shredded by"; *suffix = "'s shrapnel"; return true;
    case 8: *verb = "ate"; *suffix = "'s rocket"; return true;
    case 9: *verb = "almost dodged"; *suffix = "'s rocket"; return true;
    case 10: *verb = "was melted by"; *suffix = "'s hyperblaster"; return true;
    case 11: *verb = "was railed by"; *suffix = ""; return true;
    case 12: *verb = "saw the pretty lights from"; *suffix = "'s BFG"; return true;
    case 13: *verb = "was disintegrated by"; *suffix = "'s BFG blast"; return true;
    case 14: *verb = "couldn't hide from"; *suffix = "'s BFG"; return true;
    case 15: *verb = "caught"; *suffix = "'s handgrenade"; return true;
    case 16: *verb = "didn't see"; *suffix = "'s handgrenade"; return true;
    case 24: *verb = "feels"; *suffix = "'s pain"; return true;
    case 21: *verb = "tried to invade"; *suffix = "'s personal space"; return true;
    default: return false;
  }
}

void qa_q2_obituary(qa_q2_base_player_state *victim, qa_q2_base_player_state *attacker,
                    double means, bool deathmatch, bool coop,
                    void *score_ctx, void (*score)(void *ctx, qa_q2_base_player_state *recipient, double change),
                    char *out, size_t out_size) {
  /* TS bitwise operators work on int32 values. */
  int32_t means32 = qa_js_to_int32(means);
  bool friendly = (means32 & 0x8000000) != 0 || (coop && attacker != NULL);
  int32_t mod = means32 & ~0x8000000;
  const char *message = environment_message(mod);
  char self_message[96];
  if (attacker == victim && victim != NULL) {
    bool female = strcmp(victim->gender, "female") == 0;
    bool neutral = strcmp(victim->gender, "neutral") == 0;
    const char *possessive = female ? "her" : neutral ? "its" : "his";
    const char *reflexive = female ? "herself" : neutral ? "itself" : "himself";
    if (mod == 24) message = "tried to put the pin back in";
    else if (mod == 7 || mod == 16) { (void)snprintf(self_message, sizeof self_message, "tripped on %s own grenade", possessive); message = self_message; }
    else if (mod == 9) { (void)snprintf(self_message, sizeof self_message, "blew %s up", reflexive); message = self_message; }
    else if (mod == 13) message = "should have used a smaller gun";
    else { (void)snprintf(self_message, sizeof self_message, "killed %s", reflexive); message = self_message; }
  }
  if ((deathmatch || coop) && message != NULL) {
    if (deathmatch) { if (score) score(score_ctx, victim, -1); else victim->score += -1; }
    (void)snprintf(out, out_size, "%s %s.\n", victim->name, message);
    return;
  }
  const char *verb = NULL, *suffix = NULL;
  if ((deathmatch || coop) && attacker != NULL && kill_message(mod, &verb, &suffix)) {
    if (deathmatch) { if (score) score(score_ctx, attacker, friendly ? -1 : 1); else attacker->score += friendly ? -1 : 1; }
    (void)snprintf(out, out_size, "%s %s %s%s\n", victim->name, verb, attacker->name, suffix);
    return;
  }
  if (deathmatch) { if (score) score(score_ctx, victim, -1); else victim->score += -1; }
  (void)snprintf(out, out_size, "%s died.\n", victim->name);
}
