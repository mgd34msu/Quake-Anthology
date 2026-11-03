/* Row-audit regressions for Q2 mission-pack monster registration
 * (missionpacks/monsters/index.ts Q2MissionPackMonsters) and deathball rules
 * (modes/deathball.ts q2DeathBallRules). Expected classname
 * lists come from running the TypeScript definitions under bun
 * (scratch oracle mpmon.ts). No host callback runs during registration; the
 * hooks carry a sentinel ctx that is checked if one ever does.
 * GPL-2.0-or-later. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/foundation/monsters/index.h"
#include "qa/content/q2/missionpacks/modes/deathball.h"
#include "qa/content/q2/missionpacks/monsters/index.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                              \
  do {                                                                           \
    checks++;                                                                    \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

static char SENTINEL;
static int ctx_errors = 0, gravity_calls = 0;
static double gravity(void *ctx) { if (ctx != (void *)&SENTINEL) ctx_errors++; gravity_calls++; return 800; }

static const char *const XATRIX[] = {"monster_gekk", "monster_fixbot", "monster_gladb", "monster_boss5", "monster_chick_heat",
  "monster_soldier_ripper", "monster_soldier_hypergun", "monster_soldier_lasergun", "monster_brain", "monster_infantry",
  "monster_supertank"};
static const char *const ROGUE[] = {"monster_stalker", "monster_flyer", "monster_kamikaze", "monster_hover", "monster_daedalus",
  "monster_turret", "monster_carrier", "monster_medic", "monster_medic_commander", "monster_soldier_light", "monster_soldier",
  "monster_soldier_ss", "monster_infantry", "monster_brain", "monster_floater", "monster_gladiator", "monster_boss2",
  "monster_supertank", "monster_jorg", "monster_makron", "monster_tank", "monster_tank_commander", "monster_chick",
  "monster_berserk", "monster_mutant", "monster_parasite", "monster_widow", "monster_widow2", "monster_gunner"};
static const char *const ROGUE_FALLBACKS[] = {"monster_stalker", "monster_kamikaze", "monster_daedalus", "monster_turret",
  "monster_carrier", "monster_medic_commander", "monster_widow", "monster_widow2"};

static void make(qa_q2m_monsters *monsters, qa_q2mpm_monsters_module *module, qa_q2mpm_pack pack, qa_q2_edition edition) {
  qa_q2m_weapons weapons; memset(&weapons, 0, sizeof weapons);
  qa_q2m_monster_hooks hooks; memset(&hooks, 0, sizeof hooks);
  CHECK(qa_q2m_monsters_init(monsters, weapons, hooks));
  qa_q2mpm_services services; memset(&services, 0, sizeof services);
  services.ctx = &SENTINEL; services.gravity = gravity;
  qa_q2mpm_weapons mp_weapons; memset(&mp_weapons, 0, sizeof mp_weapons);
  mp_weapons.ctx = &SENTINEL;
  CHECK(qa_q2mpm_monsters_init(module, pack, monsters, services, &mp_weapons, edition));
}

/* Classic rogue: every TS definition (including the rogue gunner) is
 * registered for "classic"; the original fallbacks also edition-free; rogue
 * hint paths and rogue combat rules are installed at construction. */
static void test_rogue_classic(void) {
  static qa_q2m_monsters monsters; static qa_q2mpm_monsters_module module;
  make(&monsters, &module, QA_Q2MPM_PACK_ROGUE, QA_Q2_EDITION_CLASSIC);
  CHECK(module.definition_count == sizeof ROGUE / sizeof ROGUE[0]);
  for (size_t i = 0; i < module.definition_count && i < sizeof ROGUE / sizeof ROGUE[0]; i++)
    CHECK(strcmp(module.definitions[i].classname, ROGUE[i]) == 0);
  CHECK(monsters.edition_counts[0] == sizeof ROGUE / sizeof ROGUE[0]);
  CHECK(monsters.edition_counts[1] == 0);
  CHECK(monsters.definition_count == sizeof ROGUE_FALLBACKS / sizeof ROGUE_FALLBACKS[0]);
  for (size_t i = 0; i < monsters.definition_count && i < 8; i++)
    CHECK(strcmp(monsters.definitions[i]->classname, ROGUE_FALLBACKS[i]) == 0);
  CHECK(module.original_source_fallback_count == 8);
  CHECK(monsters.source_combat_rules == QA_Q2M_RULES_ROGUE);
  CHECK(monsters.hint_hooks == &module.hints.hooks);
  qa_q2mpm_monsters_destroy(&module);
  qa_q2m_monsters_destroy(&monsters);
}

/* Rerelease xatrix: only the original-source fallbacks register (edition
 * free); boss5 carries the boss explosion callbacks; base combat rules. */
static void test_xatrix_rerelease(void) {
  static qa_q2m_monsters monsters; static qa_q2mpm_monsters_module module;
  make(&monsters, &module, QA_Q2MPM_PACK_XATRIX, QA_Q2_EDITION_RERELEASE);
  CHECK(module.definition_count == sizeof XATRIX / sizeof XATRIX[0]);
  for (size_t i = 0; i < module.definition_count && i < sizeof XATRIX / sizeof XATRIX[0]; i++)
    CHECK(strcmp(module.definitions[i].classname, XATRIX[i]) == 0);
  CHECK(monsters.edition_counts[0] == 0 && monsters.edition_counts[1] == 0);
  /* TS q2OriginalMissionPackFallbacks no longer lists the soldierh variants (the rerelease builds them from the
   * base soldier family), so gekk, fixbot, gladb, boss5 and chick_heat register. */
  CHECK(monsters.definition_count == 5);
  for (size_t i = 0; i < monsters.definition_count && i < 5; i++)
    CHECK(strcmp(monsters.definitions[i]->classname, XATRIX[i]) == 0);
  bool explode = false;
  for (size_t i = 0; i < module.definitions[3].callback_count; i++)
    if (strcmp(module.definitions[3].callbacks[i].name, "BossExplode2") == 0) explode = true;
  CHECK(explode);
  CHECK(!module.has_hints && monsters.hint_hooks == NULL);
  CHECK(monsters.source_combat_rules == QA_Q2M_RULES_BASE);
  qa_q2mpm_monsters_destroy(&module);
  qa_q2m_monsters_destroy(&monsters);
}

/* TS q2DeathBallRules ORs into a signed int32. Oracle (bun):
 * 0 -> 917824, 2^31 -> -2146565824, 2^32+5 -> 917829, -1 -> -1. */
static void test_deathball_rules(void) {
  CHECK(qa_q2mp_deathball_get_rules(0).deathmatch_flags == 917824);
  CHECK(qa_q2mp_deathball_get_rules(2147483648.0).deathmatch_flags == -2146565824);
  CHECK(qa_q2mp_deathball_get_rules(4294967301.0).deathmatch_flags == 917829);
  CHECK(qa_q2mp_deathball_get_rules(-1).deathmatch_flags == -1);
  CHECK(qa_q2mp_deathball_get_rules(0).stop_speed == 0);
}

int main(void) {
  test_deathball_rules();
  test_rogue_classic();
  test_xatrix_rerelease();
  CHECK(ctx_errors == 0 && gravity_calls == 0);
  printf("test_q2_rows_mp: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
