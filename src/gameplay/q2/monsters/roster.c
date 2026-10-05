#include "internal.h"

#define BOUNDS(x1, y1, z1, x2, y2, z2)                                         \
  {                                                                            \
    .mins = {x1, y1, z1}, .maxs = { x2, y2, z2 }                               \
  }

#define MONSTER(cls, mdl, set, kind, move_kind, box, hp, gib, weight, size,    \
                eye, turn, bits, first, stand, walk, run, attack, attack2,     \
                melee, pain1, pain2, pain3, death1, death2, primary_kind,      \
                secondary_kind, primary_points, secondary_points, speed)       \
  {.classname = cls,                                                           \
   .model = mdl,                                                               \
   .move_set = set,                                                            \
   .species = kind,                                                            \
   .locomotion = move_kind,                                                    \
   .bounds = box,                                                              \
   .health = hp,                                                               \
   .gib_health = gib,                                                          \
   .mass = weight,                                                             \
   .scale = size,                                                              \
   .view_height = eye,                                                         \
   .yaw_speed = turn,                                                          \
   .flags = bits,                                                              \
   .initial_move = first,                                                      \
   .stand_move = stand,                                                        \
   .walk_move = walk,                                                          \
   .run_move = run,                                                            \
   .attack_move = attack,                                                      \
   .attack2_move = attack2,                                                    \
   .melee_move = melee,                                                        \
   .pain1_move = pain1,                                                        \
   .pain2_move = pain2,                                                        \
   .pain3_move = pain3,                                                        \
   .death1_move = death1,                                                      \
   .death2_move = death2,                                                      \
   .primary = primary_kind,                                                    \
   .secondary = secondary_kind,                                                \
   .primary_damage = primary_points,                                           \
   .secondary_damage = secondary_points,                                       \
   .projectile_speed = speed}

static const q2m_definition definitions[] = {
    MONSTER(
        "monster_infantry", "models/monsters/infantry/tris.md2", NULL,
        Q2M_INFANTRY, Q2M_WALK, BOUNDS(-16, -16, -24, 16, 16, 32), 100, -40,
        200, 1, 0, 20,
        Q2M_HAS_MELEE | Q2M_HAS_RANGED | Q2M_DUCKS | Q2M_SIDESTEPS | Q2M_JUMPS,
        Q2M_MOVE_infantry_move_stand, Q2M_MOVE_infantry_move_stand, Q2M_MOVE_infantry_move_walk,
        Q2M_MOVE_infantry_move_run, Q2M_MOVE_infantry_move_attack1, Q2M_MOVE_infantry_move_attack2,
        Q2M_MOVE_NONE, Q2M_MOVE_infantry_move_pain1, Q2M_MOVE_infantry_move_pain2, Q2M_MOVE_NONE,
        Q2M_MOVE_infantry_move_death1, Q2M_MOVE_infantry_move_death2, Q2M_ATTACK_BULLET,
        Q2M_ATTACK_HIT, 3, 15, 0),
    MONSTER("monster_soldier_light", "models/monsters/soldier/tris.md2", NULL,
            Q2M_SOLDIER_LIGHT, Q2M_WALK, BOUNDS(-16, -16, -24, 16, 16, 32), 20,
            -30, 100, 1, 0, 20,
            Q2M_HAS_RANGED | Q2M_BLIND_FIRE | Q2M_DUCKS | Q2M_SIDESTEPS,
            Q2M_MOVE_soldier_move_stand1, Q2M_MOVE_soldier_move_stand1, Q2M_MOVE_soldier_move_walk1,
            Q2M_MOVE_soldier_move_run, Q2M_MOVE_soldier_move_attack1, Q2M_MOVE_soldier_move_attack2,
            Q2M_MOVE_NONE, Q2M_MOVE_soldier_move_pain1, Q2M_MOVE_soldier_move_pain2,
            Q2M_MOVE_soldier_move_pain3, Q2M_MOVE_soldier_move_death1, Q2M_MOVE_soldier_move_death2,
            Q2M_ATTACK_BLASTER, Q2M_ATTACK_NONE, 5, 0, 600),
    MONSTER("monster_soldier", "models/monsters/soldier/tris.md2", NULL,
            Q2M_SOLDIER, Q2M_WALK, BOUNDS(-16, -16, -24, 16, 16, 32), 30, -30,
            100, 1, 0, 20,
            Q2M_HAS_RANGED | Q2M_BLIND_FIRE | Q2M_DUCKS | Q2M_SIDESTEPS,
            Q2M_MOVE_soldier_move_stand1, Q2M_MOVE_soldier_move_stand1, Q2M_MOVE_soldier_move_walk1,
            Q2M_MOVE_soldier_move_run, Q2M_MOVE_soldier_move_attack1, Q2M_MOVE_soldier_move_attack2,
            Q2M_MOVE_NONE, Q2M_MOVE_soldier_move_pain1, Q2M_MOVE_soldier_move_pain2,
            Q2M_MOVE_soldier_move_pain3, Q2M_MOVE_soldier_move_death1, Q2M_MOVE_soldier_move_death2,
            Q2M_ATTACK_SHOTGUN, Q2M_ATTACK_NONE, 4, 0, 0),
    MONSTER("monster_soldier_ss", "models/monsters/soldier/tris.md2", NULL,
            Q2M_SOLDIER_SS, Q2M_WALK, BOUNDS(-16, -16, -24, 16, 16, 32), 40,
            -30, 100, 1, 0, 20, Q2M_HAS_RANGED | Q2M_DUCKS | Q2M_SIDESTEPS,
            Q2M_MOVE_soldier_move_stand1, Q2M_MOVE_soldier_move_stand1, Q2M_MOVE_soldier_move_walk1,
            Q2M_MOVE_soldier_move_run, Q2M_MOVE_soldier_move_attack1, Q2M_MOVE_soldier_move_attack2,
            Q2M_MOVE_NONE, Q2M_MOVE_soldier_move_pain1, Q2M_MOVE_soldier_move_pain2,
            Q2M_MOVE_soldier_move_pain3, Q2M_MOVE_soldier_move_death1, Q2M_MOVE_soldier_move_death2,
            Q2M_ATTACK_BULLET, Q2M_ATTACK_NONE, 3, 0, 0),
    MONSTER("monster_berserk", "models/monsters/berserk/tris.md2", NULL,
            Q2M_BERSERK, Q2M_WALK, BOUNDS(-16, -16, -24, 16, 16, 32), 240, -60,
            250, 1, 0, 20, Q2M_HAS_MELEE | Q2M_JUMPS, Q2M_MOVE_berserk_move_stand,
            Q2M_MOVE_berserk_move_stand, Q2M_MOVE_berserk_move_walk, Q2M_MOVE_berserk_move_run1,
            Q2M_MOVE_berserk_move_attack_spike, Q2M_MOVE_berserk_move_attack_club,
            Q2M_MOVE_berserk_move_attack_spike, Q2M_MOVE_berserk_move_pain1,
            Q2M_MOVE_berserk_move_pain2, Q2M_MOVE_NONE, Q2M_MOVE_berserk_move_death1,
            Q2M_MOVE_berserk_move_death2, Q2M_ATTACK_HIT, Q2M_ATTACK_HIT, 15, 20, 0),
    MONSTER("monster_brain", "models/monsters/brain/tris.md2", NULL, Q2M_BRAIN,
            Q2M_WALK, BOUNDS(-16, -16, -24, 16, 16, 32), 300, -150, 400, 1, 0,
            20, Q2M_HAS_MELEE | Q2M_DUCKS | Q2M_POWER_SCREEN,
            Q2M_MOVE_brain_move_stand, Q2M_MOVE_brain_move_stand, Q2M_MOVE_brain_move_walk1,
            Q2M_MOVE_brain_move_run, Q2M_MOVE_brain_move_attack1, Q2M_MOVE_brain_move_attack2,
            Q2M_MOVE_brain_move_attack1, Q2M_MOVE_brain_move_pain1, Q2M_MOVE_brain_move_pain2,
            Q2M_MOVE_brain_move_pain3, Q2M_MOVE_brain_move_death1, Q2M_MOVE_brain_move_death2,
            Q2M_ATTACK_HIT, Q2M_ATTACK_BEAM, 15, 15, 0),
    MONSTER("monster_chick", "models/monsters/bitch/tris.md2", NULL, Q2M_CHICK,
            Q2M_WALK, BOUNDS(-16, -16, 0, 16, 16, 56), 175, -70, 200, 1, 0, 20,
            Q2M_HAS_MELEE | Q2M_HAS_RANGED | Q2M_BLIND_FIRE | Q2M_DUCKS,
            Q2M_MOVE_chick_move_stand, Q2M_MOVE_chick_move_stand, Q2M_MOVE_chick_move_walk,
            Q2M_MOVE_chick_move_run, Q2M_MOVE_chick_move_start_attack1, Q2M_MOVE_NONE,
            Q2M_MOVE_chick_move_start_slash, Q2M_MOVE_chick_move_pain1, Q2M_MOVE_chick_move_pain2,
            Q2M_MOVE_chick_move_pain3, Q2M_MOVE_chick_move_death1, Q2M_MOVE_chick_move_death2,
            Q2M_ATTACK_ROCKET, Q2M_ATTACK_HIT, 50, 10, 500),
    MONSTER("monster_flipper", "models/monsters/flipper/tris.md2", NULL,
            Q2M_FLIPPER, Q2M_SWIM, BOUNDS(-16, -16, 0, 16, 16, 32), 50, -30,
            100, 1, 10, 10, Q2M_HAS_MELEE, Q2M_MOVE_flipper_move_stand,
            Q2M_MOVE_flipper_move_stand, Q2M_MOVE_flipper_move_walk, Q2M_MOVE_flipper_move_start_run,
            Q2M_MOVE_flipper_move_attack, Q2M_MOVE_NONE, Q2M_MOVE_flipper_move_attack,
            Q2M_MOVE_flipper_move_pain1, Q2M_MOVE_flipper_move_pain2, Q2M_MOVE_NONE,
            Q2M_MOVE_flipper_move_death, Q2M_MOVE_NONE, Q2M_ATTACK_HIT, Q2M_ATTACK_NONE, 5, 0,
            0),
    MONSTER("monster_floater", "models/monsters/float/tris.md2", NULL,
            Q2M_FLOATER, Q2M_FLY, BOUNDS(-24, -24, -24, 24, 24, 32), 200, -80,
            300, 1, 0, 10, Q2M_HAS_MELEE | Q2M_HAS_RANGED,
            Q2M_MOVE_floater_move_stand1, Q2M_MOVE_floater_move_stand1, Q2M_MOVE_floater_move_walk,
            Q2M_MOVE_floater_move_run, Q2M_MOVE_floater_move_attack1, Q2M_MOVE_floater_move_attack2,
            Q2M_MOVE_floater_move_attack3, Q2M_MOVE_floater_move_pain1, Q2M_MOVE_floater_move_pain2,
            Q2M_MOVE_floater_move_pain3, Q2M_MOVE_floater_move_death, Q2M_MOVE_NONE,
            Q2M_ATTACK_BLASTER, Q2M_ATTACK_HIT, 5, 5, 1000),
    MONSTER("monster_flyer", "models/monsters/flyer/tris.md2", NULL, Q2M_FLYER,
            Q2M_FLY, BOUNDS(-16, -16, -24, 16, 16, 32), 50, 0, 50, 1, 0, 10,
            Q2M_HAS_MELEE | Q2M_HAS_RANGED | Q2M_SIDESTEPS, Q2M_MOVE_flyer_move_stand,
            Q2M_MOVE_flyer_move_stand, Q2M_MOVE_flyer_move_walk, Q2M_MOVE_flyer_move_run,
            Q2M_MOVE_flyer_move_attack2, Q2M_MOVE_NONE, Q2M_MOVE_flyer_move_start_melee,
            Q2M_MOVE_flyer_move_pain1, Q2M_MOVE_flyer_move_pain2, Q2M_MOVE_flyer_move_pain3,
            Q2M_MOVE_flyer_move_pain1, Q2M_MOVE_NONE, Q2M_ATTACK_BLASTER, Q2M_ATTACK_HIT, 1, 5,
            1000),
    MONSTER("monster_gladiator", "models/monsters/gladiatr/tris.md2", NULL,
            Q2M_GLADIATOR, Q2M_WALK, BOUNDS(-32, -32, -24, 32, 32, 64), 400,
            -175, 400, 1, 0, 20, Q2M_HAS_MELEE | Q2M_HAS_RANGED,
            Q2M_MOVE_gladiator_move_stand, Q2M_MOVE_gladiator_move_stand,
            Q2M_MOVE_gladiator_move_walk, Q2M_MOVE_gladiator_move_run,
            Q2M_MOVE_gladiator_move_attack_gun, Q2M_MOVE_NONE, Q2M_MOVE_gladiator_move_attack_melee,
            Q2M_MOVE_gladiator_move_pain, Q2M_MOVE_gladiator_move_pain_air, Q2M_MOVE_NONE,
            Q2M_MOVE_gladiator_move_death, Q2M_MOVE_NONE, Q2M_ATTACK_RAIL, Q2M_ATTACK_HIT, 50,
            20, 0),
    MONSTER("monster_gunner", "models/monsters/gunner/tris.md2", NULL,
            Q2M_GUNNER, Q2M_WALK, BOUNDS(-16, -16, -24, 16, 16, 32), 175, -70,
            200, 1, 0, 20,
            Q2M_HAS_RANGED | Q2M_DUCKS | Q2M_SIDESTEPS | Q2M_JUMPS,
            Q2M_MOVE_gunner_move_stand, Q2M_MOVE_gunner_move_stand, Q2M_MOVE_gunner_move_walk,
            Q2M_MOVE_gunner_move_run, Q2M_MOVE_gunner_move_attack_chain,
            Q2M_MOVE_gunner_move_attack_grenade, Q2M_MOVE_NONE, Q2M_MOVE_gunner_move_pain1,
            Q2M_MOVE_gunner_move_pain2, Q2M_MOVE_gunner_move_pain3, Q2M_MOVE_gunner_move_death, Q2M_MOVE_NONE,
            Q2M_ATTACK_BULLET, Q2M_ATTACK_GRENADE, 3, 50, 600),
    MONSTER("monster_hover", "models/monsters/hover/tris.md2", NULL, Q2M_HOVER,
            Q2M_FLY, BOUNDS(-24, -24, -24, 24, 24, 32), 240, -100, 150, 1, 0,
            10, Q2M_HAS_RANGED | Q2M_SIDESTEPS, Q2M_MOVE_hover_move_stand,
            Q2M_MOVE_hover_move_stand, Q2M_MOVE_hover_move_walk, Q2M_MOVE_hover_move_run,
            Q2M_MOVE_hover_move_start_attack, Q2M_MOVE_hover_move_start_attack2, Q2M_MOVE_NONE,
            Q2M_MOVE_hover_move_pain1, Q2M_MOVE_hover_move_pain2, Q2M_MOVE_hover_move_pain3,
            Q2M_MOVE_hover_move_death1, Q2M_MOVE_NONE, Q2M_ATTACK_BLASTER, Q2M_ATTACK_BLASTER,
            1, 1, 1000),
    MONSTER(
        "monster_jorg", "models/monsters/boss3/jorg/tris.md2", NULL, Q2M_JORG,
        Q2M_WALK, BOUNDS(-80, -80, 0, 80, 80, 140), 3000, -2000, 1000, 1, 0, 20,
        Q2M_HAS_RANGED | Q2M_BOSS | Q2M_METALLIC | Q2M_EXPLODES,
        Q2M_MOVE_jorg_move_stand, Q2M_MOVE_jorg_move_stand, Q2M_MOVE_jorg_move_walk, Q2M_MOVE_jorg_move_run,
        Q2M_MOVE_jorg_move_start_attack1, Q2M_MOVE_jorg_move_attack2, Q2M_MOVE_NONE, Q2M_MOVE_jorg_move_pain1,
        Q2M_MOVE_jorg_move_pain2, Q2M_MOVE_jorg_move_pain3, Q2M_MOVE_jorg_move_death, Q2M_MOVE_NONE,
        Q2M_ATTACK_BULLET, Q2M_ATTACK_BFG, 6, 50, 400),
    MONSTER("monster_makron", "models/monsters/boss3/rider/tris.md2", NULL,
            Q2M_MAKRON, Q2M_WALK, BOUNDS(-30, -30, 0, 30, 30, 90), 3000, -2000,
            500, 1, 0, 20,
            Q2M_HAS_RANGED | Q2M_BOSS | Q2M_METALLIC | Q2M_EXPLODES,
            Q2M_MOVE_makron_move_stand, Q2M_MOVE_makron_move_stand, Q2M_MOVE_makron_move_walk,
            Q2M_MOVE_makron_move_run, Q2M_MOVE_makron_move_attack3, Q2M_MOVE_makron_move_attack4,
            Q2M_MOVE_NONE, Q2M_MOVE_makron_move_pain4, Q2M_MOVE_makron_move_pain5, Q2M_MOVE_makron_move_pain6,
            Q2M_MOVE_makron_move_death2, Q2M_MOVE_makron_move_death3, Q2M_ATTACK_BLASTER,
            Q2M_ATTACK_RAIL, 15, 50, 1000),
    MONSTER("monster_medic", "models/monsters/medic/tris.md2", NULL, Q2M_MEDIC,
            Q2M_WALK, BOUNDS(-24, -24, -24, 24, 24, 32), 300, -130, 400, 1, 0,
            20, Q2M_HAS_RANGED | Q2M_DUCKS, Q2M_MOVE_medic_move_stand,
            Q2M_MOVE_medic_move_stand, Q2M_MOVE_medic_move_walk, Q2M_MOVE_medic_move_run,
            Q2M_MOVE_medic_move_attackBlaster, Q2M_MOVE_medic_move_callReinforcements, Q2M_MOVE_NONE,
            Q2M_MOVE_medic_move_pain1, Q2M_MOVE_medic_move_pain2, Q2M_MOVE_NONE, Q2M_MOVE_medic_move_death,
            Q2M_MOVE_NONE, Q2M_ATTACK_BLASTER, Q2M_ATTACK_SUMMON, 2, 0, 1000),
    MONSTER("monster_mutant", "models/monsters/mutant/tris.md2", NULL,
            Q2M_MUTANT, Q2M_WALK, BOUNDS(-32, -32, -24, 32, 32, 48), 300, -120,
            300, 1, 0, 20, Q2M_HAS_MELEE | Q2M_TOUCH_ATTACK | Q2M_JUMPS,
            Q2M_MOVE_mutant_move_stand, Q2M_MOVE_mutant_move_stand, Q2M_MOVE_mutant_move_walk,
            Q2M_MOVE_mutant_move_run, Q2M_MOVE_mutant_move_jump, Q2M_MOVE_NONE, Q2M_MOVE_mutant_move_attack,
            Q2M_MOVE_mutant_move_pain1, Q2M_MOVE_mutant_move_pain2, Q2M_MOVE_mutant_move_pain3,
            Q2M_MOVE_mutant_move_death1, Q2M_MOVE_mutant_move_death2, Q2M_ATTACK_HIT,
            Q2M_ATTACK_HIT, 10, 40, 0),
    MONSTER("monster_parasite", "models/monsters/parasite/tris.md2", NULL,
            Q2M_PARASITE, Q2M_WALK, BOUNDS(-16, -16, -24, 16, 16, 24), 175, -50,
            250, 1, 0, 20, Q2M_HAS_RANGED | Q2M_JUMPS, Q2M_MOVE_parasite_move_stand,
            Q2M_MOVE_parasite_move_stand, Q2M_MOVE_parasite_move_walk, Q2M_MOVE_parasite_move_run,
            Q2M_MOVE_parasite_move_drain, Q2M_MOVE_NONE, Q2M_MOVE_NONE, Q2M_MOVE_parasite_move_pain1, Q2M_MOVE_NONE,
            Q2M_MOVE_NONE, Q2M_MOVE_parasite_move_death, Q2M_MOVE_NONE, Q2M_ATTACK_BEAM, Q2M_ATTACK_NONE,
            5, 0, 0),
    MONSTER("monster_supertank", "models/monsters/boss1/tris.md2", NULL,
            Q2M_SUPERTANK, Q2M_WALK, BOUNDS(-64, -64, 0, 64, 64, 112), 1500,
            -500, 800, 1, 0, 20,
            Q2M_HAS_RANGED | Q2M_BOSS | Q2M_METALLIC | Q2M_EXPLODES,
            Q2M_MOVE_supertank_move_stand, Q2M_MOVE_supertank_move_stand,
            Q2M_MOVE_supertank_move_forward, Q2M_MOVE_supertank_move_run,
            Q2M_MOVE_supertank_move_attack1, Q2M_MOVE_supertank_move_attack2, Q2M_MOVE_NONE,
            Q2M_MOVE_supertank_move_pain1, Q2M_MOVE_supertank_move_pain2,
            Q2M_MOVE_supertank_move_pain3, Q2M_MOVE_supertank_move_death, Q2M_MOVE_NONE,
            Q2M_ATTACK_ROCKET, Q2M_ATTACK_BULLET, 50, 6, 500),
    MONSTER("monster_tank", "models/monsters/tank/tris.md2", NULL, Q2M_TANK,
            Q2M_WALK, BOUNDS(-32, -32, -16, 32, 32, 72), 750, -200, 500, 1, 0,
            20, Q2M_HAS_RANGED | Q2M_METALLIC | Q2M_BLIND_FIRE,
            Q2M_MOVE_tank_move_stand, Q2M_MOVE_tank_move_stand, Q2M_MOVE_tank_move_walk,
            Q2M_MOVE_tank_move_start_run, Q2M_MOVE_tank_move_attack_blast,
            Q2M_MOVE_tank_move_attack_pre_rocket, Q2M_MOVE_NONE, Q2M_MOVE_tank_move_pain1,
            Q2M_MOVE_tank_move_pain2, Q2M_MOVE_tank_move_pain3, Q2M_MOVE_tank_move_death, Q2M_MOVE_NONE,
            Q2M_ATTACK_BLASTER, Q2M_ATTACK_ROCKET, 30, 50, 650),
    MONSTER("monster_tank_commander", "models/monsters/tank/tris.md2", NULL,
            Q2M_TANK_COMMANDER, Q2M_WALK, BOUNDS(-32, -32, -16, 32, 32, 72),
            1000, -225, 500, 1, 0, 20,
            Q2M_HAS_RANGED | Q2M_METALLIC | Q2M_BLIND_FIRE, Q2M_MOVE_tank_move_stand,
            Q2M_MOVE_tank_move_stand, Q2M_MOVE_tank_move_walk, Q2M_MOVE_tank_move_start_run,
            Q2M_MOVE_tank_move_attack_blast, Q2M_MOVE_tank_move_attack_pre_rocket, Q2M_MOVE_NONE,
            Q2M_MOVE_tank_move_pain1, Q2M_MOVE_tank_move_pain2, Q2M_MOVE_tank_move_pain3,
            Q2M_MOVE_tank_move_death, Q2M_MOVE_NONE, Q2M_ATTACK_BLASTER, Q2M_ATTACK_ROCKET, 30,
            50, 650),
    MONSTER("monster_boss2", "models/monsters/boss2/tris.md2", NULL, Q2M_BOSS2,
            Q2M_FLY, BOUNDS(-56, -56, 0, 56, 56, 80), 2000, -200, 1000, 1, 0,
            20, Q2M_HAS_RANGED | Q2M_BOSS | Q2M_METALLIC | Q2M_EXPLODES,
            Q2M_MOVE_boss2_move_stand, Q2M_MOVE_boss2_move_stand, Q2M_MOVE_boss2_move_walk,
            Q2M_MOVE_boss2_move_run, Q2M_MOVE_boss2_move_attack_pre_mg,
            Q2M_MOVE_boss2_move_attack_rocket, Q2M_MOVE_NONE, Q2M_MOVE_boss2_move_pain_light,
            Q2M_MOVE_boss2_move_pain_heavy, Q2M_MOVE_NONE, Q2M_MOVE_boss2_move_death, Q2M_MOVE_NONE,
            Q2M_ATTACK_BULLET, Q2M_ATTACK_ROCKET, 6, 50, 500),
    MONSTER("misc_actor", "players/male/tris.md2", NULL, Q2M_ACTOR, Q2M_WALK,
            BOUNDS(-16, -16, -24, 16, 16, 32), 100, -80, 200, 1, 0, 20,
            Q2M_HAS_RANGED | Q2M_GOOD_GUY, Q2M_MOVE_actor_move_stand,
            Q2M_MOVE_actor_move_stand, Q2M_MOVE_actor_move_walk, Q2M_MOVE_actor_move_run,
            Q2M_MOVE_actor_move_attack, Q2M_MOVE_NONE, Q2M_MOVE_NONE, Q2M_MOVE_actor_move_pain1,
            Q2M_MOVE_actor_move_pain2, Q2M_MOVE_actor_move_pain3, Q2M_MOVE_actor_move_death1,
            Q2M_MOVE_actor_move_death2, Q2M_ATTACK_BULLET, Q2M_ATTACK_NONE, 3, 0, 0),
    MONSTER("misc_insane", "models/monsters/insane/tris.md2", NULL, Q2M_INSANE,
            Q2M_WALK, BOUNDS(-16, -16, -24, 16, 16, 32), 100, -50, 300, 1, 0,
            20, Q2M_GOOD_GUY, Q2M_MOVE_insane_move_stand_normal,
            Q2M_MOVE_insane_move_stand_normal, Q2M_MOVE_insane_move_walk_normal,
            Q2M_MOVE_insane_move_run_normal, Q2M_MOVE_NONE, Q2M_MOVE_NONE, Q2M_MOVE_NONE,
            Q2M_MOVE_insane_move_stand_pain, Q2M_MOVE_insane_move_crawl_pain, Q2M_MOVE_NONE,
            Q2M_MOVE_insane_move_stand_death, Q2M_MOVE_insane_move_crawl_death,
            Q2M_ATTACK_NONE, Q2M_ATTACK_NONE, 0, 0, 0),
    MONSTER("monster_gekk", "models/monsters/gekk/tris.md2",
            "missionpacks/xatrix-gekk:gekkMoves", Q2M_GEKK, Q2M_WALK,
            BOUNDS(-24, -24, -24, 24, 24, 24), 125, -30, 300, 1, 0, 20,
            Q2M_HAS_MELEE | Q2M_HAS_RANGED | Q2M_TOUCH_ATTACK | Q2M_JUMPS,
            Q2M_MOVE_gekk_move_stand, Q2M_MOVE_gekk_move_stand, Q2M_MOVE_gekk_move_walk,
            Q2M_MOVE_gekk_move_run, Q2M_MOVE_gekk_move_spit, Q2M_MOVE_gekk_move_leapatk,
            Q2M_MOVE_gekk_move_attack1, Q2M_MOVE_gekk_move_pain, Q2M_MOVE_gekk_move_pain1,
            Q2M_MOVE_gekk_move_pain2, Q2M_MOVE_gekk_move_death1, Q2M_MOVE_gekk_move_death3,
            Q2M_ATTACK_BLASTER, Q2M_ATTACK_HIT, 10, 15, 550),
    MONSTER("monster_fixbot", "models/monsters/fixbot/tris.md2",
            "missionpacks/xatrix-fixbot:fixbotMoves", Q2M_FIXBOT, Q2M_FLY,
            BOUNDS(-32, -32, -24, 32, 32, 24), 150, 0, 150, 1, 0, 10,
            Q2M_HAS_RANGED, Q2M_MOVE_fixbot_move_stand, Q2M_MOVE_fixbot_move_stand,
            Q2M_MOVE_fixbot_move_walk, Q2M_MOVE_fixbot_move_run, Q2M_MOVE_fixbot_move_start_attack,
            Q2M_MOVE_fixbot_move_laserattack, Q2M_MOVE_NONE, Q2M_MOVE_fixbot_move_paina,
            Q2M_MOVE_fixbot_move_painb, Q2M_MOVE_fixbot_move_pain3, Q2M_MOVE_fixbot_move_death1,
            Q2M_MOVE_NONE, Q2M_ATTACK_BLASTER, Q2M_ATTACK_BEAM, 15, 15, 1000),
    MONSTER("monster_gladb", "models/monsters/gladb/tris.md2",
            "missionpacks/xatrix-gladb:gladbMoves", Q2M_GLADB, Q2M_WALK,
            BOUNDS(-32, -32, -24, 32, 32, 64), 800, -175, 350, 1, 0, 20,
            Q2M_HAS_MELEE | Q2M_HAS_RANGED | Q2M_POWER_SHIELD,
            Q2M_MOVE_gladb_move_stand, Q2M_MOVE_gladb_move_stand, Q2M_MOVE_gladb_move_walk,
            Q2M_MOVE_gladb_move_run, Q2M_MOVE_gladb_move_attack_gun, Q2M_MOVE_NONE,
            Q2M_MOVE_gladb_move_attack_melee, Q2M_MOVE_gladb_move_pain, Q2M_MOVE_gladb_move_pain_air,
            Q2M_MOVE_NONE, Q2M_MOVE_gladb_move_death, Q2M_MOVE_NONE, Q2M_ATTACK_BLASTER, Q2M_ATTACK_HIT,
            100, 20, 725),
    MONSTER("monster_boss5", "models/monsters/boss5/tris.md2",
            "missionpacks/xatrix-boss5:boss5Moves", Q2M_BOSS5, Q2M_WALK,
            BOUNDS(-64, -64, 0, 64, 64, 112), 1500, -500, 800, 1, 0, 20,
            Q2M_HAS_RANGED | Q2M_BOSS | Q2M_METALLIC | Q2M_POWER_SHIELD |
                Q2M_EXPLODES,
            Q2M_MOVE_boss5_move_stand, Q2M_MOVE_boss5_move_stand, Q2M_MOVE_boss5_move_forward,
            Q2M_MOVE_boss5_move_run, Q2M_MOVE_boss5_move_attack1, Q2M_MOVE_boss5_move_attack2, Q2M_MOVE_NONE,
            Q2M_MOVE_boss5_move_pain1, Q2M_MOVE_boss5_move_pain2, Q2M_MOVE_boss5_move_pain3,
            Q2M_MOVE_boss5_move_death, Q2M_MOVE_NONE, Q2M_ATTACK_ROCKET, Q2M_ATTACK_BULLET, 50,
            6, 500),
    MONSTER("monster_chick_heat", "models/monsters/bitch2/tris.md2", NULL,
            Q2M_CHICK_HEAT, Q2M_WALK, BOUNDS(-16, -16, 0, 16, 16, 56), 175, -70,
            200, 1, 0, 20,
            Q2M_HAS_MELEE | Q2M_HAS_RANGED | Q2M_BLIND_FIRE | Q2M_DUCKS,
            Q2M_MOVE_chick_move_stand, Q2M_MOVE_chick_move_stand, Q2M_MOVE_chick_move_walk,
            Q2M_MOVE_chick_move_run, Q2M_MOVE_chick_move_start_attack1, Q2M_MOVE_NONE,
            Q2M_MOVE_chick_move_start_slash, Q2M_MOVE_chick_move_pain1, Q2M_MOVE_chick_move_pain2,
            Q2M_MOVE_chick_move_pain3, Q2M_MOVE_chick_move_death1, Q2M_MOVE_chick_move_death2,
            Q2M_ATTACK_HEAT, Q2M_ATTACK_HIT, 50, 10, 500),
    MONSTER("monster_soldier_ripper", "models/monsters/soldierh/tris.md2",
            "missionpacks/xatrix-soldierh:soldierhMoves", Q2M_SOLDIER_RIPPER,
            Q2M_WALK, BOUNDS(-16, -16, -24, 16, 16, 32), 50, -30, 100, 1, 0, 20,
            Q2M_HAS_RANGED | Q2M_BLIND_FIRE | Q2M_DUCKS, Q2M_MOVE_soldierh_move_stand3,
            Q2M_MOVE_soldierh_move_stand1, Q2M_MOVE_soldierh_move_walk1, Q2M_MOVE_soldierh_move_run,
            Q2M_MOVE_soldierh_move_attack1, Q2M_MOVE_soldierh_move_attack2, Q2M_MOVE_NONE,
            Q2M_MOVE_soldierh_move_pain1, Q2M_MOVE_soldierh_move_pain2, Q2M_MOVE_soldierh_move_pain3,
            Q2M_MOVE_soldierh_move_death1, Q2M_MOVE_soldierh_move_death2, Q2M_ATTACK_ION,
            Q2M_ATTACK_NONE, 5, 0, 600),
    MONSTER("monster_soldier_hypergun", "models/monsters/soldierh/tris.md2",
            "missionpacks/xatrix-soldierh:soldierhMoves", Q2M_SOLDIER_HYPER,
            Q2M_WALK, BOUNDS(-16, -16, -24, 16, 16, 32), 60, -30, 100, 1, 0, 20,
            Q2M_HAS_RANGED | Q2M_DUCKS, Q2M_MOVE_soldierh_move_stand3,
            Q2M_MOVE_soldierh_move_stand1, Q2M_MOVE_soldierh_move_walk1, Q2M_MOVE_soldierh_move_run,
            Q2M_MOVE_soldierh_move_attack1, Q2M_MOVE_soldierh_move_attack2, Q2M_MOVE_NONE,
            Q2M_MOVE_soldierh_move_pain1, Q2M_MOVE_soldierh_move_pain2, Q2M_MOVE_soldierh_move_pain3,
            Q2M_MOVE_soldierh_move_death1, Q2M_MOVE_soldierh_move_death2,
            Q2M_ATTACK_BLUE_BOLT, Q2M_ATTACK_NONE, 1, 0, 600),
    MONSTER("monster_soldier_lasergun", "models/monsters/soldierh/tris.md2",
            "missionpacks/xatrix-soldierh:soldierhMoves", Q2M_SOLDIER_LASER,
            Q2M_WALK, BOUNDS(-16, -16, -24, 16, 16, 32), 70, -30, 100, 1, 0, 20,
            Q2M_HAS_RANGED | Q2M_DUCKS, Q2M_MOVE_soldierh_move_stand3,
            Q2M_MOVE_soldierh_move_stand1, Q2M_MOVE_soldierh_move_walk1, Q2M_MOVE_soldierh_move_run,
            Q2M_MOVE_soldierh_move_attack1, Q2M_MOVE_soldierh_move_attack2, Q2M_MOVE_NONE,
            Q2M_MOVE_soldierh_move_pain1, Q2M_MOVE_soldierh_move_pain2, Q2M_MOVE_soldierh_move_pain3,
            Q2M_MOVE_soldierh_move_death1, Q2M_MOVE_soldierh_move_death2, Q2M_ATTACK_BEAM,
            Q2M_ATTACK_NONE, 1, 0, 1000),
    MONSTER("monster_stalker", "models/monsters/stalker/tris.md2",
            "missionpacks/rogue-stalker:stalkerMoves", Q2M_STALKER, Q2M_WALK,
            BOUNDS(-28, -28, -18, 28, 28, 18), 250, -50, 250, 1, 0, 20,
            Q2M_HAS_MELEE | Q2M_HAS_RANGED | Q2M_JUMPS | Q2M_REGENERATES,
            Q2M_MOVE_stalker_move_stand, Q2M_MOVE_stalker_move_stand, Q2M_MOVE_stalker_move_walk,
            Q2M_MOVE_stalker_move_run, Q2M_MOVE_stalker_move_shoot, Q2M_MOVE_NONE,
            Q2M_MOVE_stalker_move_swing_l, Q2M_MOVE_stalker_move_pain, Q2M_MOVE_NONE, Q2M_MOVE_NONE,
            Q2M_MOVE_stalker_move_death, Q2M_MOVE_NONE, Q2M_ATTACK_GREEN_BOLT, Q2M_ATTACK_HIT,
            15, 5, 800),
    MONSTER("monster_kamikaze", "models/monsters/flyer/tris.md2",
            "missionpacks/rogue-flyer:flyerMoves", Q2M_KAMIKAZE, Q2M_FLY,
            BOUNDS(-16, -16, -24, 16, 16, 16), 50, 0, 100, 1, 0, 10,
            Q2M_TOUCH_ATTACK | Q2M_EXPLODES | Q2M_DO_NOT_COUNT,
            Q2M_MOVE_flyer_move_kamikaze, Q2M_MOVE_flyer_move_stand, Q2M_MOVE_flyer_move_walk,
            Q2M_MOVE_flyer_move_kamikaze, Q2M_MOVE_flyer_move_kamikaze, Q2M_MOVE_NONE, Q2M_MOVE_NONE,
            Q2M_MOVE_flyer_move_pain1, Q2M_MOVE_flyer_move_pain2, Q2M_MOVE_flyer_move_pain3,
            Q2M_MOVE_flyer_move_pain1, Q2M_MOVE_NONE, Q2M_ATTACK_NONE, Q2M_ATTACK_NONE, 0, 0,
            0),
    MONSTER("monster_daedalus", "models/monsters/hover/tris.md2",
            "missionpacks/rogue-hover:hoverMoves", Q2M_DAEDALUS, Q2M_FLY,
            BOUNDS(-24, -24, -24, 24, 24, 32), 450, -100, 225, 1, 0, 25,
            Q2M_HAS_RANGED | Q2M_SIDESTEPS, Q2M_MOVE_hover_move_stand,
            Q2M_MOVE_hover_move_stand, Q2M_MOVE_hover_move_walk, Q2M_MOVE_hover_move_run,
            Q2M_MOVE_hover_move_start_attack, Q2M_MOVE_hover_move_start_attack2, Q2M_MOVE_NONE,
            Q2M_MOVE_hover_move_pain1, Q2M_MOVE_hover_move_pain2, Q2M_MOVE_hover_move_pain3,
            Q2M_MOVE_hover_move_death1, Q2M_MOVE_NONE, Q2M_ATTACK_GREEN_BOLT,
            Q2M_ATTACK_GREEN_BOLT, 1, 1, 1000),
    MONSTER("monster_turret", "models/monsters/turret/tris.md2",
            "missionpacks/rogue-turret:turretMoves", Q2M_TURRET, Q2M_STATIONARY,
            BOUNDS(-12, -12, -12, 12, 12, 12), 240, -100, 250, 1, 0, 45,
            Q2M_HAS_RANGED | Q2M_BLIND_FIRE | Q2M_METALLIC, Q2M_MOVE_turret_move_stand,
            Q2M_MOVE_turret_move_stand, Q2M_MOVE_turret_move_stand, Q2M_MOVE_turret_move_run,
            Q2M_MOVE_turret_move_fire, Q2M_MOVE_turret_move_fire_blind, Q2M_MOVE_NONE, Q2M_MOVE_NONE, Q2M_MOVE_NONE,
            Q2M_MOVE_NONE, Q2M_MOVE_turret_move_stand, Q2M_MOVE_NONE, Q2M_ATTACK_ROCKET,
            Q2M_ATTACK_BULLET, 50, 3, 650),
    MONSTER("monster_carrier", "models/monsters/carrier/tris.md2",
            "missionpacks/rogue-carrier:carrierMoves", Q2M_CARRIER, Q2M_FLY,
            BOUNDS(-56, -56, -44, 56, 56, 44), 2000, -200, 1000, 1, 0, 15,
            Q2M_HAS_RANGED | Q2M_BLIND_FIRE | Q2M_BOSS | Q2M_EXPLODES |
                Q2M_NO_GIB,
            Q2M_MOVE_carrier_move_stand, Q2M_MOVE_carrier_move_stand, Q2M_MOVE_carrier_move_walk,
            Q2M_MOVE_carrier_move_run, Q2M_MOVE_carrier_move_attack_pre_mg,
            Q2M_MOVE_carrier_move_attack_rocket, Q2M_MOVE_NONE, Q2M_MOVE_carrier_move_pain_light,
            Q2M_MOVE_carrier_move_pain_heavy, Q2M_MOVE_NONE, Q2M_MOVE_carrier_move_death, Q2M_MOVE_NONE,
            Q2M_ATTACK_BULLET, Q2M_ATTACK_ROCKET, 6, 50, 750),
    MONSTER("monster_medic_commander", "models/monsters/medic/tris.md2",
            "missionpacks/rogue-medic:medicMoves", Q2M_MEDIC_COMMANDER,
            Q2M_WALK, BOUNDS(-24, -24, -24, 24, 24, 32), 600, -130, 600, 1, 0,
            40, Q2M_HAS_RANGED | Q2M_DUCKS | Q2M_POWER_SHIELD,
            Q2M_MOVE_medic_move_stand, Q2M_MOVE_medic_move_stand, Q2M_MOVE_medic_move_walk,
            Q2M_MOVE_medic_move_run, Q2M_MOVE_medic_move_attackBlaster,
            Q2M_MOVE_medic_move_callReinforcements, Q2M_MOVE_NONE, Q2M_MOVE_medic_move_pain1,
            Q2M_MOVE_medic_move_pain2, Q2M_MOVE_NONE, Q2M_MOVE_medic_move_death, Q2M_MOVE_NONE,
            Q2M_ATTACK_GREEN_BOLT, Q2M_ATTACK_SUMMON, 3, 0, 1000),
    MONSTER("monster_widow", "models/monsters/blackwidow/tris.md2",
            "missionpacks/rogue-widow:widowMoves", Q2M_WIDOW, Q2M_WALK,
            BOUNDS(-40, -40, 0, 40, 40, 144), 2000, -5000, 1500, 1, 0, 30,
            Q2M_HAS_MELEE | Q2M_HAS_RANGED | Q2M_BOSS | Q2M_NO_GIB |
                Q2M_POWER_SHIELD,
            Q2M_MOVE_widow_move_stand, Q2M_MOVE_widow_move_stand, Q2M_MOVE_widow_move_walk,
            Q2M_MOVE_widow_move_run, Q2M_MOVE_widow_move_attack_pre_blaster,
            Q2M_MOVE_widow_move_attack_pre_rail, Q2M_MOVE_widow_move_attack_kick,
            Q2M_MOVE_widow_move_pain_light, Q2M_MOVE_widow_move_pain_heavy, Q2M_MOVE_NONE,
            Q2M_MOVE_widow_move_death, Q2M_MOVE_NONE, Q2M_ATTACK_GREEN_BOLT, Q2M_ATTACK_RAIL,
            10, 50, 1000),
    MONSTER("monster_widow2", "models/monsters/blackwidow2/tris.md2",
            "missionpacks/rogue-widow2:widow2Moves", Q2M_WIDOW2, Q2M_WALK,
            BOUNDS(-70, -70, 0, 70, 70, 144), 2800, -900, 2500, 1, 0, 30,
            Q2M_HAS_MELEE | Q2M_HAS_RANGED | Q2M_BOSS | Q2M_NO_GIB |
                Q2M_POWER_SHIELD,
            Q2M_MOVE_widow2_move_stand, Q2M_MOVE_widow2_move_stand, Q2M_MOVE_widow2_move_walk,
            Q2M_MOVE_widow2_move_run, Q2M_MOVE_widow2_move_attack_pre_beam,
            Q2M_MOVE_widow2_move_attack_disrupt, Q2M_MOVE_widow2_move_tongs,
            Q2M_MOVE_widow2_move_pain, Q2M_MOVE_NONE, Q2M_MOVE_NONE, Q2M_MOVE_widow2_move_death, Q2M_MOVE_NONE,
            Q2M_ATTACK_BEAM, Q2M_ATTACK_TRACKER, 15, 20, 0),
    MONSTER("monster_arachnid", "models/monsters/arachnid/tris.md2",
            "rerelease/arachnid:arachnidMoves", Q2M_ARACHNID, Q2M_WALK,
            BOUNDS(-48, -48, -20, 48, 48, 48), 1000, -200, 450, 1, 0, 20,
            Q2M_HAS_MELEE | Q2M_HAS_RANGED, Q2M_MOVE_arachnid_move_stand,
            Q2M_MOVE_arachnid_move_stand, Q2M_MOVE_arachnid_move_walk, Q2M_MOVE_arachnid_move_run,
            Q2M_MOVE_arachnid_attack1, Q2M_MOVE_arachnid_attack_up1, Q2M_MOVE_arachnid_melee,
            Q2M_MOVE_arachnid_move_pain1, Q2M_MOVE_arachnid_move_pain2, Q2M_MOVE_NONE,
            Q2M_MOVE_arachnid_move_death, Q2M_MOVE_NONE, Q2M_ATTACK_RAIL, Q2M_ATTACK_HIT, 35,
            15, 0),
    MONSTER("monster_guardian", "models/monsters/guardian/tris.md2",
            "rerelease/guardian:guardianMoves", Q2M_GUARDIAN, Q2M_WALK,
            BOUNDS(-96, -96, -66, 96, 96, 62), 2500, -200, 850, 1, 0, 20,
            Q2M_HAS_MELEE | Q2M_HAS_RANGED | Q2M_BOSS | Q2M_NO_GIB |
                Q2M_METALLIC,
            Q2M_MOVE_guardian_move_stand, Q2M_MOVE_guardian_move_stand, Q2M_MOVE_guardian_move_walk,
            Q2M_MOVE_guardian_move_run, Q2M_MOVE_guardian_move_atk1_in,
            Q2M_MOVE_guardian_move_atk2_in, Q2M_MOVE_guardian_move_kick,
            Q2M_MOVE_guardian_move_pain1, Q2M_MOVE_NONE, Q2M_MOVE_NONE, Q2M_MOVE_guardian_move_death, Q2M_MOVE_NONE,
            Q2M_ATTACK_BLASTER, Q2M_ATTACK_BEAM, 2, 25, 1000),
    MONSTER("monster_guncmdr", "models/monsters/gunner/tris.md2",
            "rerelease/guncmdr:guncmdrMoves", Q2M_GUN_COMMANDER, Q2M_WALK,
            BOUNDS(-16, -16, -24, 16, 16, 36), 325, -175, 255, 1.15f, 37, 20,
            Q2M_HAS_MELEE | Q2M_HAS_RANGED | Q2M_DUCKS | Q2M_SIDESTEPS |
                Q2M_JUMPS,
            Q2M_MOVE_guncmdr_move_stand, Q2M_MOVE_guncmdr_move_stand, Q2M_MOVE_guncmdr_move_walk,
            Q2M_MOVE_guncmdr_move_run, Q2M_MOVE_guncmdr_move_attack_chain,
            Q2M_MOVE_guncmdr_move_attack_mortar, Q2M_MOVE_guncmdr_move_attack_kick,
            Q2M_MOVE_guncmdr_move_pain1, Q2M_MOVE_guncmdr_move_pain2, Q2M_MOVE_guncmdr_move_pain3,
            Q2M_MOVE_guncmdr_move_death1, Q2M_MOVE_guncmdr_move_death2, Q2M_ATTACK_FLECHETTE,
            Q2M_ATTACK_GRENADE, 4, 50, 800),
    MONSTER("monster_shambler", "models/monsters/shambler/tris.md2",
            "rerelease/shambler:shamblerMoves", Q2M_SHAMBLER, Q2M_WALK,
            BOUNDS(-32, -32, -24, 32, 32, 64), 600, -60, 500, 1, 0, 20,
            Q2M_HAS_MELEE | Q2M_HAS_RANGED, Q2M_MOVE_shambler_move_stand,
            Q2M_MOVE_shambler_move_stand, Q2M_MOVE_shambler_move_walk, Q2M_MOVE_shambler_move_run,
            Q2M_MOVE_shambler_attack_magic, Q2M_MOVE_shambler_attack_smash,
            Q2M_MOVE_shambler_attack_swingl, Q2M_MOVE_shambler_move_pain, Q2M_MOVE_NONE, Q2M_MOVE_NONE,
            Q2M_MOVE_shambler_move_death, Q2M_MOVE_NONE, Q2M_ATTACK_BEAM, Q2M_ATTACK_HIT, 10,
            40, 0),
    MONSTER("turret_driver", "models/monsters/infantry/tris.md2", NULL,
            Q2M_TURRET_DRIVER, Q2M_STATIONARY,
            BOUNDS(-16, -16, -24, 16, 16, 32), 100, 0, 200, 1, 24, 20,
            Q2M_HAS_RANGED | Q2M_DO_NOT_COUNT, Q2M_MOVE_infantry_move_stand,
            Q2M_MOVE_infantry_move_stand, Q2M_MOVE_infantry_move_walk, Q2M_MOVE_infantry_move_run,
            Q2M_MOVE_infantry_move_attack1, Q2M_MOVE_NONE, Q2M_MOVE_NONE, Q2M_MOVE_infantry_move_pain1,
            Q2M_MOVE_infantry_move_pain2, Q2M_MOVE_NONE, Q2M_MOVE_infantry_move_death1, Q2M_MOVE_NONE,
            Q2M_ATTACK_BULLET, Q2M_ATTACK_NONE, 3, 0, 0),
    MONSTER("monster_tank_stand", "models/monsters/tank/tris.md2",
            "rerelease/tank:tankMoves", Q2M_TANK_STAND, Q2M_STATIONARY,
            BOUNDS(-32, -32, -16, 32, 32, 64), 1, 0, 750, 1, 0, 0,
            Q2M_NO_GIB | Q2M_DO_NOT_COUNT, Q2M_MOVE_tank_move_stand, Q2M_MOVE_tank_move_stand,
            Q2M_MOVE_tank_move_stand, Q2M_MOVE_tank_move_stand, Q2M_MOVE_NONE, Q2M_MOVE_NONE, Q2M_MOVE_NONE, Q2M_MOVE_NONE, Q2M_MOVE_NONE,
            Q2M_MOVE_NONE, Q2M_MOVE_NONE, Q2M_MOVE_NONE, Q2M_ATTACK_NONE, Q2M_ATTACK_NONE, 0, 0, 0),
    MONSTER("monster_boss3_stand", "models/monsters/boss3/rider/tris.md2",
            "base/boss32:boss32Moves", Q2M_BOSS3_STAND, Q2M_STATIONARY,
            BOUNDS(-32, -32, 0, 32, 32, 90), 1, 0, 500, 1, 0, 0,
            Q2M_NO_GIB | Q2M_DO_NOT_COUNT, Q2M_MOVE_makron_move_stand,
            Q2M_MOVE_makron_move_stand, Q2M_MOVE_makron_move_stand, Q2M_MOVE_makron_move_stand, Q2M_MOVE_NONE,
            Q2M_MOVE_NONE, Q2M_MOVE_NONE, Q2M_MOVE_NONE, Q2M_MOVE_NONE, Q2M_MOVE_NONE, Q2M_MOVE_NONE, Q2M_MOVE_NONE, Q2M_ATTACK_NONE,
            Q2M_ATTACK_NONE, 0, 0, 0),
};

static const q2m_definition rerelease_jorg = MONSTER(
    "monster_jorg", "models/monsters/boss3/jorg/tris.md2",
    "rerelease/boss31:boss31Moves", Q2M_JORG, Q2M_WALK,
    BOUNDS(-80, -80, 0, 80, 80, 140), 8000, -2000, 1000, 1, 0, 20,
    Q2M_HAS_RANGED | Q2M_BOSS | Q2M_METALLIC | Q2M_EXPLODES, Q2M_MOVE_jorg_move_stand,
    Q2M_MOVE_jorg_move_stand, Q2M_MOVE_jorg_move_walk, Q2M_MOVE_jorg_move_run,
    Q2M_MOVE_jorg_move_start_attack1, Q2M_MOVE_jorg_move_attack2, Q2M_MOVE_NONE, Q2M_MOVE_jorg_move_pain1,
    Q2M_MOVE_jorg_move_pain2, Q2M_MOVE_jorg_move_pain3, Q2M_MOVE_jorg_move_death, Q2M_MOVE_NONE,
    Q2M_ATTACK_BULLET, Q2M_ATTACK_BFG, 6, 50, 400);

static const q2m_definition rerelease_gunner =
    MONSTER("monster_gunner", "models/monsters/gunner/tris.md2",
            "rerelease/gunner:gunnerMoves", Q2M_GUNNER, Q2M_WALK,
            BOUNDS(-16, -16, -24, 16, 16, 32), 175, -70, 200, 1.15f, 0, 20,
            Q2M_HAS_RANGED | Q2M_DUCKS | Q2M_SIDESTEPS | Q2M_JUMPS,
            Q2M_MOVE_gunner_move_stand, Q2M_MOVE_gunner_move_stand, Q2M_MOVE_gunner_move_walk,
            Q2M_MOVE_gunner_move_run, Q2M_MOVE_gunner_move_attack_chain,
            Q2M_MOVE_gunner_move_attack_grenade, Q2M_MOVE_NONE, Q2M_MOVE_gunner_move_pain1,
            Q2M_MOVE_gunner_move_pain2, Q2M_MOVE_gunner_move_pain3, Q2M_MOVE_gunner_move_death, Q2M_MOVE_NONE,
            Q2M_ATTACK_BULLET, Q2M_ATTACK_GRENADE, 3, 50, 600);

static const q2m_definition rerelease_gladb = MONSTER(
    "monster_gladb", "models/monsters/gladb/tris.md2",
    "rerelease/gladiator:gladiatorMoves", Q2M_GLADB, Q2M_WALK,
    BOUNDS(-32, -32, -24, 32, 32, 64), 250, -175, 350, 1, 0, 20,
    Q2M_HAS_MELEE | Q2M_HAS_RANGED | Q2M_POWER_SHIELD, Q2M_MOVE_gladiator_move_stand,
    Q2M_MOVE_gladiator_move_stand, Q2M_MOVE_gladiator_move_walk, Q2M_MOVE_gladiator_move_run,
    Q2M_MOVE_gladb_move_attack_gun, Q2M_MOVE_NONE, Q2M_MOVE_gladiator_move_attack_melee,
    Q2M_MOVE_gladiator_move_pain, Q2M_MOVE_gladiator_move_pain_air, Q2M_MOVE_NONE,
    Q2M_MOVE_gladiator_move_death, Q2M_MOVE_NONE, Q2M_ATTACK_PLASMA, Q2M_ATTACK_HIT, 100, 20,
    725);

#define RERELEASE_SOLDIERH(name, species, health, attack, damage, speed) \
  MONSTER(name, "models/monsters/soldier/tris.md2", \
          "foundation/moves:rerelease_soldierMoves", species, Q2M_WALK, \
          BOUNDS(-16, -16, -24, 16, 16, 32), health, -30, 100, 1, 0, 20, \
          Q2M_HAS_RANGED | Q2M_DUCKS | Q2M_SIDESTEPS | \
              (species == Q2M_SOLDIER_LASER ? 0 : Q2M_BLIND_FIRE), \
          Q2M_MOVE_soldier_move_stand1, Q2M_MOVE_soldier_move_stand1, \
          Q2M_MOVE_soldier_move_walk1, Q2M_MOVE_soldier_move_start_run, \
          Q2M_MOVE_soldierh_move_attack1, Q2M_MOVE_soldierh_move_attack2, Q2M_MOVE_NONE, \
          Q2M_MOVE_soldier_move_pain1, Q2M_MOVE_soldier_move_pain2, Q2M_MOVE_soldier_move_pain3, \
          Q2M_MOVE_soldier_move_death1, Q2M_MOVE_soldier_move_death2, attack, \
          Q2M_ATTACK_NONE, damage, 0, speed)
static const q2m_definition rerelease_soldier_ripper =
    RERELEASE_SOLDIERH("monster_soldier_ripper", Q2M_SOLDIER_RIPPER,
                      50, Q2M_ATTACK_ION, 5, 600);
static const q2m_definition rerelease_soldier_hyper =
    RERELEASE_SOLDIERH("monster_soldier_hypergun", Q2M_SOLDIER_HYPER,
                      60, Q2M_ATTACK_BLUE_BOLT, 1, 600);
static const q2m_definition rerelease_soldier_laser =
    RERELEASE_SOLDIERH("monster_soldier_lasergun", Q2M_SOLDIER_LASER,
                      70, Q2M_ATTACK_BEAM, 1, 1000);
#undef RERELEASE_SOLDIERH

static const q2m_definition *find_definition(const char *classname) {
  for (size_t i = 0; i < sizeof(definitions) / sizeof(definitions[0]); ++i)
    if (strcmp(definitions[i].classname, classname) == 0)
      return &definitions[i];
  return NULL;
}

const q2m_definition *q2m_definition_for(const qa_q2_game *game,
                                         const char *classname) {
  if (game == NULL || classname == NULL)
    return NULL;
  if (game->options.edition == QA_Q2_RERELEASE) {
    if (strcmp(classname, "monster_soldier_ripper") == 0)
      return &rerelease_soldier_ripper;
    if (strcmp(classname, "monster_soldier_hypergun") == 0)
      return &rerelease_soldier_hyper;
    if (strcmp(classname, "monster_soldier_lasergun") == 0)
      return &rerelease_soldier_laser;
    if (strcmp(classname, "monster_jorg") == 0)
      return &rerelease_jorg;
    if (strcmp(classname, "monster_gunner") == 0)
      return &rerelease_gunner;
    if (strcmp(classname, "monster_gladb") == 0)
      return &rerelease_gladb;
  }
  return find_definition(classname);
}

static const char *base_move_set(const qa_q2_game *game, q2m_species species) {
  bool rerelease = game->options.edition == QA_Q2_RERELEASE;
  bool rogue = game->options.product == QA_Q2_ROGUE;
  bool xatrix = game->options.product == QA_Q2_XATRIX;
  switch (species) {
  case Q2M_INFANTRY:
  case Q2M_TURRET_DRIVER:
    return rerelease ? "foundation/moves:rerelease_infantryMoves"
           : rogue   ? "missionpacks/rogue-infantry:infantryMoves"
           : xatrix  ? "missionpacks/xatrix-infantry:infantryMoves"
                     : "foundation/moves:classic_infantryMoves";
  case Q2M_SOLDIER_LIGHT:
  case Q2M_SOLDIER:
  case Q2M_SOLDIER_SS:
    return rerelease ? "foundation/moves:rerelease_soldierMoves"
           : rogue   ? "missionpacks/rogue-soldier:soldierMoves"
                     : "foundation/moves:classic_soldierMoves";
  case Q2M_BERSERK:
    return rerelease ? "rerelease/berserk:berserkMoves"
           : rogue   ? "missionpacks/rogue-berserk:berserkMoves"
                     : "base/berserk:berserkMoves";
  case Q2M_BRAIN:
    return rerelease ? "rerelease/brain:brainMoves"
           : xatrix  ? "missionpacks/xatrix-brain:brainMoves"
           : rogue   ? "missionpacks/rogue-brain:brainMoves"
                     : "base/brain:brainMoves";
  case Q2M_CHICK:
  case Q2M_CHICK_HEAT:
    return rerelease ? "rerelease/chick:chickMoves"
           : rogue   ? "missionpacks/rogue-chick:chickMoves"
                     : "base/chick:chickMoves";
  case Q2M_FLIPPER:
    return rerelease ? "rerelease/flipper:flipperMoves"
                     : "base/flipper:flipperMoves";
  case Q2M_FLOATER:
    return rerelease ? "rerelease/float:floatMoves"
           : rogue   ? "missionpacks/rogue-float:floatMoves"
                     : "base/float:floatMoves";
  case Q2M_FLYER:
    return rerelease ? "rerelease/flyer:flyerMoves"
           : rogue   ? "missionpacks/rogue-flyer:flyerMoves"
                     : "base/flyer:flyerMoves";
  case Q2M_GLADIATOR:
    return rerelease ? "rerelease/gladiator:gladiatorMoves"
                     : "base/gladiator:gladiatorMoves";
  case Q2M_GUNNER:
    return rerelease ? "rerelease/gunner:gunnerMoves"
           : rogue   ? "missionpacks/rogue-gunner:gunnerMoves"
                     : "base/gunner:gunnerMoves";
  case Q2M_HOVER:
    return rerelease ? "rerelease/hover:hoverMoves"
           : rogue   ? "missionpacks/rogue-hover:hoverMoves"
                     : "base/hover:hoverMoves";
  case Q2M_JORG:
    return rerelease ? "rerelease/boss31:boss31Moves"
                     : "base/boss31:boss31Moves";
  case Q2M_MAKRON:
    return rerelease ? "rerelease/boss32:boss32Moves"
                     : "base/boss32:boss32Moves";
  case Q2M_MEDIC:
    return rerelease ? "rerelease/medic:medicMoves"
           : rogue   ? "missionpacks/rogue-medic:medicMoves"
                     : "base/medic:medicMoves";
  case Q2M_MUTANT:
    return rerelease ? "rerelease/mutant:mutantMoves"
           : rogue   ? "missionpacks/rogue-mutant:mutantMoves"
                     : "base/mutant:mutantMoves";
  case Q2M_PARASITE:
    return rerelease ? "rerelease/parasite:parasiteMoves"
           : rogue   ? "missionpacks/rogue-parasite:parasiteMoves"
                     : "base/parasite:parasiteMoves";
  case Q2M_SUPERTANK:
    return rerelease ? "rerelease/supertank:supertankMoves"
                     : "base/supertank:supertankMoves";
  case Q2M_TANK:
  case Q2M_TANK_COMMANDER:
    return rerelease ? "rerelease/tank:tankMoves" : "base/tank:tankMoves";
  case Q2M_BOSS2:
    return rerelease ? "rerelease/boss2:boss2Moves" : "base/boss2:boss2Moves";
  case Q2M_ACTOR:
    return rerelease ? "rerelease/actor:actorMoves" : "base/actor:actorMoves";
  case Q2M_INSANE:
    return rerelease ? "rerelease/insane:insaneMoves"
                     : "base/insane:insaneMoves";
  default:
    return NULL;
  }
}

const q2m_move_set *q2m_move_set_for(const qa_q2_game *game,
                                     const q2m_definition *definition) {
  if (game == NULL || definition == NULL)
    return NULL;
  if (game->options.edition == QA_Q2_RERELEASE && definition->species == Q2M_STALKER)
    return q2m_stalker_rerelease_moves();
  if (game->options.edition == QA_Q2_RERELEASE &&
      (definition->species == Q2M_MEDIC || definition->species == Q2M_MEDIC_COMMANDER))
    return q2m_moves_named("rerelease/medic:medicMoves");
  const char *key = definition->move_set;
  if (key == NULL)
    key = base_move_set(game, definition->species);
  return q2m_moves_named(key);
}

const q2m_move *q2m_move_find(const struct qa_q2_monster *monster, q2m_move_id id) {
  if (id == Q2M_MOVE_NONE) return NULL;
  uint16_t ordinal = monster->move_set->move_index[id];
  return ordinal ? monster->move_set->moves + ordinal - 1 : NULL;
}

const q2m_move *q2m_move_named(const struct qa_q2_monster *monster, const char *name) {
  if (!name) return NULL;
  for (size_t i = 0; i < monster->move_set->move_count; ++i)
    if (!strcmp(q2m_move_name(monster->move_set->moves + i), name))
      return monster->move_set->moves + i;
  return NULL;
}

#undef MONSTER
#undef BOUNDS
