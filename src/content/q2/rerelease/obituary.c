/* Port of src/content/q2/rerelease/obituary.ts. GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/obituary.h"
#include <stdio.h>
#include <string.h>

typedef struct { long k; const char *v; } kv;
static const kv GENERIC[] = {{23,"suicide"},{22,"falling"},{20,"crush"},{17,"water"},{18,"slime"},{19,"lava"},
  {25,"explosive"},{26,"explosive"},{28,"exit"},{30,"laser"},{33,"blaster"},{27,"hurt"},{29,"hurt"},{31,"hurt"},{38,"gekk"},{36,"gekk"}};
static const kv SELF[] = {{24,"held_grenade"},{16,"grenade_splash"},{7,"grenade_splash"},{9,"rocket_splash"},{13,"bfg_blast"},{39,"trap"},{53,"dopple_explode"}};
static const kv KILL[] = {{1,"blaster"},{2,"shotgun"},{3,"sshotgun"},{4,"machinegun"},{5,"chaingun"},{6,"grenade"},{7,"grenade_splash"},
  {8,"rocket"},{9,"rocket_splash"},{10,"hyperblaster"},{11,"railgun"},{12,"bfg_laser"},{13,"bfg_blast"},{14,"bfg_effect"},{15,"handgrenade"},
  {16,"handgrenade_splash"},{24,"held_grenade"},{21,"telefrag"},{57,"telefrag"},{34,"ripper"},{35,"phalanx"},{39,"trap"},{40,"chainfist"},
  {41,"disintegrator"},{42,"etf_rifle"},{44,"heatbeam"},{45,"tesla"},{46,"prox"},{47,"nuke"},{48,"vengeance_sphere"},{49,"hunter_sphere"},
  {50,"defender_sphere"},{51,"tracker"},{53,"dopple_explode"},{54,"dopple_vengeance"},{55,"dopple_hunter"},{56,"grapple"}};
static const char *find_kv(const kv *t, size_t n, long k) {
  for (size_t i = 0; i < n; i++) if (t[i].k == k) return t[i].v;
  return NULL;
}
qa_q2rr_obituary qa_q2_rerelease_obituary(qa_q2rr_player_state *victim, qa_q2rr_player_state *attacker,
                                          double cause, qa_q2_mode mode, bool no_point_loss,
                                          void *score_ctx,
                                          void (*score)(void *ctx, qa_q2rr_player_state *r, double change)) {
  long c = (long)(int32_t)cause;
  long means = c & ~0x8000000L;
  bool friendly = (c & 0x8000000L) != 0 || (mode == QA_Q2_MODE_COOP && attacker);
  qa_q2rr_obituary out; memset(&out, 0, sizeof out);
  const char *source = NULL;
  char buf[96];
  if (attacker == victim && victim) {
    snprintf(buf, sizeof buf, "$g_mod_self_%s", (find_kv(SELF, sizeof SELF / sizeof *SELF, means) ? find_kv(SELF, sizeof SELF / sizeof *SELF, means) : "default"));
    source = buf;
  } else if (find_kv(GENERIC, sizeof GENERIC / sizeof *GENERIC, means)) {
    snprintf(buf, sizeof buf, "$g_mod_generic_%s", (find_kv(GENERIC, sizeof GENERIC / sizeof *GENERIC, means) ? find_kv(GENERIC, sizeof GENERIC / sizeof *GENERIC, means) : "died"));
    source = buf;
  }
#define SCORE(r, ch) do { if (score) score(score_ctx, (r), (ch)); else (r)->score += (ch); } while (0)
  if (source) {
    if (mode == QA_Q2_MODE_DEATHMATCH && !no_point_loss) SCORE(victim, -1);
    snprintf(out.text, sizeof out.text, "%s", source);
    out.args[0] = victim->name; out.arg_count = 1;
    return out;
  }
  if (attacker) {
    if (mode == QA_Q2_MODE_DEATHMATCH) { if (!friendly) SCORE(attacker, 1); else if (!no_point_loss) SCORE(attacker, -1); }
    else if (mode != QA_Q2_MODE_COOP) SCORE(victim, -1);
    snprintf(out.text, sizeof out.text, "$g_mod_kill_%s", (find_kv(KILL, sizeof KILL / sizeof *KILL, means) ? find_kv(KILL, sizeof KILL / sizeof *KILL, means) : "generic"));
    out.args[0] = victim->name; out.args[1] = attacker->name; out.arg_count = 2;
    return out;
  }
  if (mode == QA_Q2_MODE_DEATHMATCH && !no_point_loss) SCORE(victim, -1);
  snprintf(out.text, sizeof out.text, "$g_mod_generic_died");
  out.args[0] = victim->name; out.arg_count = 1;
  return out;
#undef SCORE
}
