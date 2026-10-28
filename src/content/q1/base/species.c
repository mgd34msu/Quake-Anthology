/* Port of src/content/q1/base/species.ts. Table generated
 * mechanically from a bun JSON dump of baseSpecies; count and
 * first entry cross-checked against the TS source.
 * GPL-2.0-or-later. */
#include "qa/content/q1/base/species.h"

#include <string.h>

#include "qa/foundation/support.h"

static const char *const CLASSNAMES_0[] = {"monster_knight"};
static const char *const GIBS_0[] = {"gib1", "gib2", "gib3"};
static const char *const CLASSNAMES_1[] = {"monster_enforcer"};
static const char *const GIBS_1[] = {"gib1", "gib2", "gib3"};
static const char *const CLASSNAMES_2[] = {"monster_demon1"};
static const char *const GIBS_2[] = {"gib1", "gib1", "gib1"};
static const char *const CLASSNAMES_3[] = {"monster_ogre", "monster_ogre_marksman"};
static const char *const GIBS_3[] = {"gib3", "gib3", "gib3"};
static const char *const CLASSNAMES_4[] = {"monster_hell_knight"};
static const char *const GIBS_4[] = {"gib1", "gib2", "gib3"};
static const char *const CLASSNAMES_5[] = {"monster_shambler"};
static const char *const GIBS_5[] = {"gib1", "gib2", "gib3"};
static const char *const CLASSNAMES_6[] = {"monster_wizard"};
static const char *const GIBS_6[] = {"gib2", "gib2", "gib2"};
static const char *const CLASSNAMES_7[] = {"monster_shalrath"};
static const char *const GIBS_7[] = {"gib1", "gib2", "gib3"};
static const char *const CLASSNAMES_8[] = {"monster_tarbaby"};
static const char *const CLASSNAMES_9[] = {"monster_fish"};
static const char *const CLASSNAMES_10[] = {"monster_zombie"};
static const char *const GIBS_10[] = {"gib1", "gib2", "gib3"};
static const char *const CLASSNAMES_11[] = {"monster_boss"};
static const char *const CLASSNAMES_12[] = {"monster_oldone"};
const qa_q1_base_monster_species QA_Q1_BASE_SPECIES[] = {
  {.species = "knight", .kill_string = "$qc_ks_knight", .classnames = CLASSNAMES_0, .classname_count = 1, .model = "knight", .head = "h_knight", .health = 75, .gib_health = -40, .gibs = GIBS_0, 3, .bounds = {{-16, -16, -24}, {16, 16, 40}}, .stand = "knight_stand1", .walk = "knight_walk1", .run = "knight_run1", .sight = "knight/ksight.wav", .missile = NULL, .melee = true, .movement = "walk"},
  {.species = "enforcer", .kill_string = "$qc_ks_enforcer", .classnames = CLASSNAMES_1, .classname_count = 1, .model = "enforcer", .head = "h_mega", .health = 80, .gib_health = -35, .gibs = GIBS_1, 3, .bounds = {{-16, -16, -24}, {16, 16, 40}}, .stand = "enf_stand1", .walk = "enf_walk1", .run = "enf_run1", .sight = "enforcer/sight1.wav", .missile = "enf_atk1", .melee = false, .movement = "walk"},
  {.species = "demon", .kill_string = "$qc_ks_fiend", .classnames = CLASSNAMES_2, .classname_count = 1, .model = "demon", .head = "h_demon", .health = 300, .gib_health = -80, .gibs = GIBS_2, 3, .bounds = {{-32, -32, -24}, {32, 32, 64}}, .stand = "demon1_stand1", .walk = "demon1_walk1", .run = "demon1_run1", .sight = "demon/sight2.wav", .missile = "demon1_jump1", .melee = true, .movement = "walk"},
  {.species = "ogre", .kill_string = "$qc_ks_ogre", .classnames = CLASSNAMES_3, .classname_count = 2, .model = "ogre", .head = "h_ogre", .health = 200, .gib_health = -80, .gibs = GIBS_3, 3, .bounds = {{-32, -32, -24}, {32, 32, 64}}, .stand = "ogre_stand1", .walk = "ogre_walk1", .run = "ogre_run1", .sight = "ogre/ogwake.wav", .missile = "ogre_nail1", .melee = true, .movement = "walk"},
  {.species = "hellknight", .kill_string = "$qc_ks_deathknight", .classnames = CLASSNAMES_4, .classname_count = 1, .model = "hknight", .head = "h_hellkn", .health = 250, .gib_health = -40, .gibs = GIBS_4, 3, .bounds = {{-16, -16, -24}, {16, 16, 40}}, .stand = "hknight_stand1", .walk = "hknight_walk1", .run = "hknight_run1", .sight = "hknight/sight1.wav", .missile = "hknight_magicc1", .melee = true, .movement = "walk"},
  {.species = "shambler", .kill_string = "$qc_ks_shambler", .classnames = CLASSNAMES_5, .classname_count = 1, .model = "shambler", .head = "h_shams", .health = 600, .gib_health = -60, .gibs = GIBS_5, 3, .bounds = {{-32, -32, -24}, {32, 32, 64}}, .stand = "sham_stand1", .walk = "sham_walk1", .run = "sham_run1", .sight = "shambler/ssight.wav", .missile = "sham_magic1", .melee = true, .movement = "walk"},
  {.species = "wizard", .kill_string = "$qc_ks_scrag", .classnames = CLASSNAMES_6, .classname_count = 1, .model = "wizard", .head = "h_wizard", .health = 80, .gib_health = -40, .gibs = GIBS_6, 3, .bounds = {{-16, -16, -24}, {16, 16, 40}}, .stand = "wiz_stand1", .walk = "wiz_walk1", .run = "wiz_run1", .sight = "wizard/wsight.wav", .missile = "wiz_fast1", .melee = false, .movement = "fly"},
  {.species = "shalrath", .kill_string = "$qc_ks_vore", .classnames = CLASSNAMES_7, .classname_count = 1, .model = "shalrath", .head = "h_shal", .health = 400, .gib_health = -90, .gibs = GIBS_7, 3, .bounds = {{-32, -32, -24}, {32, 32, 64}}, .stand = "shal_stand", .walk = "shal_walk1", .run = "shal_run1", .sight = "shalrath/sight.wav", .missile = "shal_attack1", .melee = false, .movement = "walk"},
  {.species = "tarbaby", .kill_string = "$qc_ks_spawn", .classnames = CLASSNAMES_8, .classname_count = 1, .model = "tarbaby", .head = NULL, .health = 80, .gib_health = (-1.0/0.0), .gibs = NULL, 0, .bounds = {{-16, -16, -24}, {16, 16, 40}}, .stand = "tbaby_stand1", .walk = "tbaby_walk1", .run = "tbaby_run1", .sight = "blob/sight1.wav", .missile = "tbaby_jump1", .melee = true, .movement = "walk"},
  {.species = "fish", .kill_string = "$qc_ks_rotfish", .classnames = CLASSNAMES_9, .classname_count = 1, .model = "fish", .head = NULL, .health = 25, .gib_health = (-1.0/0.0), .gibs = NULL, 0, .bounds = {{-16, -16, -24}, {16, 16, 24}}, .stand = "f_stand1", .walk = "f_walk1", .run = "f_run1", .sight = "fish/idle.wav", .missile = NULL, .melee = true, .movement = "swim"},
  {.species = "zombie", .kill_string = "$qc_ks_zombie", .classnames = CLASSNAMES_10, .classname_count = 1, .model = "zombie", .head = "h_zombie", .health = 60, .gib_health = 0, .gibs = GIBS_10, 3, .bounds = {{-16, -16, -24}, {16, 16, 40}}, .stand = "zombie_stand1", .walk = "zombie_walk1", .run = "zombie_run1", .sight = "zombie/z_idle.wav", .missile = "zombie_atta1", .melee = false, .movement = "walk"},
  {.species = "boss", .kill_string = "$qc_ks_chthon", .classnames = CLASSNAMES_11, .classname_count = 1, .model = "boss", .head = NULL, .health = 3, .gib_health = (-1.0/0.0), .gibs = NULL, 0, .bounds = {{-128, -128, -24}, {128, 128, 256}}, .stand = "boss_idle1", .walk = "boss_idle1", .run = "boss_missile1", .sight = "boss1/sight1.wav", .missile = "boss_missile1", .melee = false, .movement = "boss"},
  {.species = "oldone", .kill_string = "$qc_ks_shub", .classnames = CLASSNAMES_12, .classname_count = 1, .model = "oldone", .head = NULL, .health = 40000, .gib_health = (-1.0/0.0), .gibs = NULL, 0, .bounds = {{-160, -128, -24}, {160, 128, 256}}, .stand = "old_idle1", .walk = "old_idle1", .run = "old_idle1", .sight = "boss2/sight.wav", .missile = NULL, .melee = false, .movement = "boss"},
};
const size_t QA_Q1_BASE_SPECIES_COUNT = sizeof QA_Q1_BASE_SPECIES / sizeof *QA_Q1_BASE_SPECIES;

void qa_q1_precache_id1_monster(qa_q1b_game *game, const char *species) {
  if (strcmp(species, "knight") == 0) {
    game->precache_model(game, "progs/knight.mdl");
    game->precache_model(game, "progs/h_knight.mdl");
    game->precache_sound(game, "knight/kdeath.wav");
    game->precache_sound(game, "knight/khurt.wav");
    game->precache_sound(game, "knight/ksight.wav");
    game->precache_sound(game, "knight/sword1.wav");
    game->precache_sound(game, "knight/sword2.wav");
    game->precache_sound(game, "knight/idle.wav");
  }
  else if (strcmp(species, "enforcer") == 0) {
    game->precache_model(game, "progs/enforcer.mdl");
    game->precache_model(game, "progs/h_mega.mdl");
    game->precache_model(game, "progs/laser.mdl");
    game->precache_sound(game, "enforcer/death1.wav");
    game->precache_sound(game, "enforcer/enfire.wav");
    game->precache_sound(game, "enforcer/enfstop.wav");
    game->precache_sound(game, "enforcer/idle1.wav");
    game->precache_sound(game, "enforcer/pain1.wav");
    game->precache_sound(game, "enforcer/pain2.wav");
    game->precache_sound(game, "enforcer/sight1.wav");
    game->precache_sound(game, "enforcer/sight2.wav");
    game->precache_sound(game, "enforcer/sight3.wav");
    game->precache_sound(game, "enforcer/sight4.wav");
  }
  else if (strcmp(species, "demon") == 0) {
    game->precache_model(game, "progs/demon.mdl");
    game->precache_model(game, "progs/h_demon.mdl");
    game->precache_sound(game, "demon/ddeath.wav");
    game->precache_sound(game, "demon/dhit2.wav");
    game->precache_sound(game, "demon/djump.wav");
    game->precache_sound(game, "demon/dpain1.wav");
    game->precache_sound(game, "demon/idle1.wav");
    game->precache_sound(game, "demon/sight2.wav");
  }
  else if (strcmp(species, "ogre") == 0) {
    game->precache_model(game, "progs/ogre.mdl");
    game->precache_model(game, "progs/h_ogre.mdl");
    game->precache_model(game, "progs/grenade.mdl");
    game->precache_sound(game, "ogre/ogdrag.wav");
    game->precache_sound(game, "ogre/ogdth.wav");
    game->precache_sound(game, "ogre/ogidle.wav");
    game->precache_sound(game, "ogre/ogidle2.wav");
    game->precache_sound(game, "ogre/ogpain1.wav");
    game->precache_sound(game, "ogre/ogsawatk.wav");
    game->precache_sound(game, "ogre/ogwake.wav");
  }
  else if (strcmp(species, "hellknight") == 0) {
    game->precache_model(game, "progs/hknight.mdl");
    game->precache_model(game, "progs/k_spike.mdl");
    game->precache_model(game, "progs/h_hellkn.mdl");
    game->precache_sound(game, "hknight/attack1.wav");
    game->precache_sound(game, "hknight/death1.wav");
    game->precache_sound(game, "hknight/pain1.wav");
    game->precache_sound(game, "hknight/sight1.wav");
    game->precache_sound(game, "hknight/hit.wav");
    game->precache_sound(game, "hknight/slash1.wav");
    game->precache_sound(game, "hknight/idle.wav");
    game->precache_sound(game, "hknight/grunt.wav");
    game->precache_sound(game, "knight/sword1.wav");
    game->precache_sound(game, "knight/sword2.wav");
  }
  else if (strcmp(species, "shambler") == 0) {
    game->precache_model(game, "progs/shambler.mdl");
    game->precache_model(game, "progs/s_light.mdl");
    game->precache_model(game, "progs/h_shams.mdl");
    game->precache_model(game, "progs/bolt.mdl");
    game->precache_sound(game, "shambler/sattck1.wav");
    game->precache_sound(game, "shambler/sboom.wav");
    game->precache_sound(game, "shambler/sdeath.wav");
    game->precache_sound(game, "shambler/shurt2.wav");
    game->precache_sound(game, "shambler/sidle.wav");
    game->precache_sound(game, "shambler/ssight.wav");
    game->precache_sound(game, "shambler/melee1.wav");
    game->precache_sound(game, "shambler/melee2.wav");
    game->precache_sound(game, "shambler/smack.wav");
  }
  else if (strcmp(species, "wizard") == 0) {
    game->precache_model(game, "progs/wizard.mdl");
    game->precache_model(game, "progs/h_wizard.mdl");
    game->precache_model(game, "progs/w_spike.mdl");
    game->precache_sound(game, "wizard/hit.wav");
    game->precache_sound(game, "wizard/wattack.wav");
    game->precache_sound(game, "wizard/wdeath.wav");
    game->precache_sound(game, "wizard/widle1.wav");
    game->precache_sound(game, "wizard/widle2.wav");
    game->precache_sound(game, "wizard/wpain.wav");
    game->precache_sound(game, "wizard/wsight.wav");
  }
  else if (strcmp(species, "shalrath") == 0) {
    game->precache_model(game, "progs/shalrath.mdl");
    game->precache_model(game, "progs/h_shal.mdl");
    game->precache_model(game, "progs/v_spike.mdl");
    game->precache_sound(game, "shalrath/attack.wav");
    game->precache_sound(game, "shalrath/attack2.wav");
    game->precache_sound(game, "shalrath/death.wav");
    game->precache_sound(game, "shalrath/idle.wav");
    game->precache_sound(game, "shalrath/pain.wav");
    game->precache_sound(game, "shalrath/sight.wav");
  }
  else if (strcmp(species, "tarbaby") == 0) {
    game->precache_model(game, "progs/tarbaby.mdl");
    game->precache_sound(game, "blob/death1.wav");
    game->precache_sound(game, "blob/hit1.wav");
    game->precache_sound(game, "blob/land1.wav");
    game->precache_sound(game, "blob/sight1.wav");
  }
  else if (strcmp(species, "fish") == 0) {
    game->precache_model(game, "progs/fish.mdl");
    game->precache_sound(game, "fish/death.wav");
    game->precache_sound(game, "fish/bite.wav");
    game->precache_sound(game, "fish/idle.wav");
  }
  else if (strcmp(species, "zombie") == 0) {
    game->precache_model(game, "progs/zombie.mdl");
    game->precache_model(game, "progs/h_zombie.mdl");
    game->precache_model(game, "progs/zom_gib.mdl");
    game->precache_sound(game, "zombie/z_idle.wav");
    game->precache_sound(game, "zombie/z_idle1.wav");
    game->precache_sound(game, "zombie/z_shot1.wav");
    game->precache_sound(game, "zombie/z_gib.wav");
    game->precache_sound(game, "zombie/z_pain.wav");
    game->precache_sound(game, "zombie/z_pain1.wav");
    game->precache_sound(game, "zombie/z_fall.wav");
    game->precache_sound(game, "zombie/z_miss.wav");
    game->precache_sound(game, "zombie/z_hit.wav");
    game->precache_sound(game, "zombie/idle_w2.wav");
  }
  else if (strcmp(species, "boss") == 0) {
    game->precache_model(game, "progs/boss.mdl");
    game->precache_model(game, "progs/lavaball.mdl");
    game->precache_sound(game, "weapons/rocket1i.wav");
    game->precache_sound(game, "boss1/out1.wav");
    game->precache_sound(game, "boss1/sight1.wav");
    game->precache_sound(game, "misc/power.wav");
    game->precache_sound(game, "boss1/throw.wav");
    game->precache_sound(game, "boss1/pain.wav");
    game->precache_sound(game, "boss1/death.wav");
  }
  else if (strcmp(species, "oldone") == 0) {
    game->precache_model(game, "progs/oldone.mdl");
    game->precache_sound(game, "boss2/death.wav");
    game->precache_sound(game, "boss2/idle.wav");
    game->precache_sound(game, "boss2/sight.wav");
    game->precache_sound(game, "boss2/pop2.wav");
  }
  else qa_range_error("No id1 base monster precaches for %s", species);
}
