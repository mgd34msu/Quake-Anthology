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
        "infantry_move_stand", "infantry_move_stand", "infantry_move_walk",
        "infantry_move_run", "infantry_move_attack1", "infantry_move_attack2",
        NULL, "infantry_move_pain1", "infantry_move_pain2", NULL,
        "infantry_move_death1", "infantry_move_death2", Q2M_ATTACK_BULLET,
        Q2M_ATTACK_HIT, 3, 15, 0),
    MONSTER("monster_soldier_light", "models/monsters/soldier/tris.md2", NULL,
            Q2M_SOLDIER_LIGHT, Q2M_WALK, BOUNDS(-16, -16, -24, 16, 16, 32), 20,
            -30, 100, 1, 0, 20,
            Q2M_HAS_RANGED | Q2M_BLIND_FIRE | Q2M_DUCKS | Q2M_SIDESTEPS,
            "soldier_move_stand1", "soldier_move_stand1", "soldier_move_walk1",
            "soldier_move_run", "soldier_move_attack1", "soldier_move_attack2",
            NULL, "soldier_move_pain1", "soldier_move_pain2",
            "soldier_move_pain3", "soldier_move_death1", "soldier_move_death2",
            Q2M_ATTACK_BLASTER, Q2M_ATTACK_NONE, 5, 0, 600),
    MONSTER("monster_soldier", "models/monsters/soldier/tris.md2", NULL,
            Q2M_SOLDIER, Q2M_WALK, BOUNDS(-16, -16, -24, 16, 16, 32), 30, -30,
            100, 1, 0, 20,
            Q2M_HAS_RANGED | Q2M_BLIND_FIRE | Q2M_DUCKS | Q2M_SIDESTEPS,
            "soldier_move_stand1", "soldier_move_stand1", "soldier_move_walk1",
            "soldier_move_run", "soldier_move_attack1", "soldier_move_attack2",
            NULL, "soldier_move_pain1", "soldier_move_pain2",
            "soldier_move_pain3", "soldier_move_death1", "soldier_move_death2",
            Q2M_ATTACK_SHOTGUN, Q2M_ATTACK_NONE, 4, 0, 0),
    MONSTER("monster_soldier_ss", "models/monsters/soldier/tris.md2", NULL,
            Q2M_SOLDIER_SS, Q2M_WALK, BOUNDS(-16, -16, -24, 16, 16, 32), 40,
            -30, 100, 1, 0, 20, Q2M_HAS_RANGED | Q2M_DUCKS | Q2M_SIDESTEPS,
            "soldier_move_stand1", "soldier_move_stand1", "soldier_move_walk1",
            "soldier_move_run", "soldier_move_attack1", "soldier_move_attack2",
            NULL, "soldier_move_pain1", "soldier_move_pain2",
            "soldier_move_pain3", "soldier_move_death1", "soldier_move_death2",
            Q2M_ATTACK_BULLET, Q2M_ATTACK_NONE, 3, 0, 0),
    MONSTER("monster_berserk", "models/monsters/berserk/tris.md2", NULL,
            Q2M_BERSERK, Q2M_WALK, BOUNDS(-16, -16, -24, 16, 16, 32), 240, -60,
            250, 1, 0, 20, Q2M_HAS_MELEE | Q2M_JUMPS, "berserk_move_stand",
            "berserk_move_stand", "berserk_move_walk", "berserk_move_run1",
            "berserk_move_attack_spike", "berserk_move_attack_club",
            "berserk_move_attack_spike", "berserk_move_pain1",
            "berserk_move_pain2", NULL, "berserk_move_death1",
            "berserk_move_death2", Q2M_ATTACK_HIT, Q2M_ATTACK_HIT, 15, 20, 0),
    MONSTER("monster_brain", "models/monsters/brain/tris.md2", NULL, Q2M_BRAIN,
            Q2M_WALK, BOUNDS(-16, -16, -24, 16, 16, 32), 300, -150, 400, 1, 0,
            20, Q2M_HAS_MELEE | Q2M_DUCKS | Q2M_POWER_SCREEN,
            "brain_move_stand", "brain_move_stand", "brain_move_walk1",
            "brain_move_run", "brain_move_attack1", "brain_move_attack2",
            "brain_move_attack1", "brain_move_pain1", "brain_move_pain2",
            "brain_move_pain3", "brain_move_death1", "brain_move_death2",
            Q2M_ATTACK_HIT, Q2M_ATTACK_BEAM, 15, 15, 0),
    MONSTER("monster_chick", "models/monsters/bitch/tris.md2", NULL, Q2M_CHICK,
            Q2M_WALK, BOUNDS(-16, -16, 0, 16, 16, 56), 175, -70, 200, 1, 0, 20,
            Q2M_HAS_MELEE | Q2M_HAS_RANGED | Q2M_BLIND_FIRE | Q2M_DUCKS,
            "chick_move_stand", "chick_move_stand", "chick_move_walk",
            "chick_move_run", "chick_move_start_attack1", NULL,
            "chick_move_start_slash", "chick_move_pain1", "chick_move_pain2",
            "chick_move_pain3", "chick_move_death1", "chick_move_death2",
            Q2M_ATTACK_ROCKET, Q2M_ATTACK_HIT, 50, 10, 500),
    MONSTER("monster_flipper", "models/monsters/flipper/tris.md2", NULL,
            Q2M_FLIPPER, Q2M_SWIM, BOUNDS(-16, -16, 0, 16, 16, 32), 50, -30,
            100, 1, 10, 10, Q2M_HAS_MELEE, "flipper_move_stand",
            "flipper_move_stand", "flipper_move_walk", "flipper_move_start_run",
            "flipper_move_attack", NULL, "flipper_move_attack",
            "flipper_move_pain1", "flipper_move_pain2", NULL,
            "flipper_move_death", NULL, Q2M_ATTACK_HIT, Q2M_ATTACK_NONE, 5, 0,
            0),
    MONSTER("monster_floater", "models/monsters/float/tris.md2", NULL,
            Q2M_FLOATER, Q2M_FLY, BOUNDS(-24, -24, -24, 24, 24, 32), 200, -80,
            300, 1, 0, 10, Q2M_HAS_MELEE | Q2M_HAS_RANGED,
            "floater_move_stand1", "floater_move_stand1", "floater_move_walk",
            "floater_move_run", "floater_move_attack1", "floater_move_attack2",
            "floater_move_attack3", "floater_move_pain1", "floater_move_pain2",
            "floater_move_pain3", "floater_move_death", NULL,
            Q2M_ATTACK_BLASTER, Q2M_ATTACK_HIT, 5, 5, 1000),
    MONSTER("monster_flyer", "models/monsters/flyer/tris.md2", NULL, Q2M_FLYER,
            Q2M_FLY, BOUNDS(-16, -16, -24, 16, 16, 32), 50, 0, 50, 1, 0, 10,
            Q2M_HAS_MELEE | Q2M_HAS_RANGED | Q2M_SIDESTEPS, "flyer_move_stand",
            "flyer_move_stand", "flyer_move_walk", "flyer_move_run",
            "flyer_move_attack2", NULL, "flyer_move_start_melee",
            "flyer_move_pain1", "flyer_move_pain2", "flyer_move_pain3",
            "flyer_move_pain1", NULL, Q2M_ATTACK_BLASTER, Q2M_ATTACK_HIT, 1, 5,
            1000),
    MONSTER("monster_gladiator", "models/monsters/gladiatr/tris.md2", NULL,
            Q2M_GLADIATOR, Q2M_WALK, BOUNDS(-32, -32, -24, 32, 32, 64), 400,
            -175, 400, 1, 0, 20, Q2M_HAS_MELEE | Q2M_HAS_RANGED,
            "gladiator_move_stand", "gladiator_move_stand",
            "gladiator_move_walk", "gladiator_move_run",
            "gladiator_move_attack_gun", NULL, "gladiator_move_attack_melee",
            "gladiator_move_pain", "gladiator_move_pain_air", NULL,
            "gladiator_move_death", NULL, Q2M_ATTACK_RAIL, Q2M_ATTACK_HIT, 50,
            20, 0),
    MONSTER("monster_gunner", "models/monsters/gunner/tris.md2", NULL,
            Q2M_GUNNER, Q2M_WALK, BOUNDS(-16, -16, -24, 16, 16, 32), 175, -70,
            200, 1, 0, 20,
            Q2M_HAS_RANGED | Q2M_DUCKS | Q2M_SIDESTEPS | Q2M_JUMPS,
            "gunner_move_stand", "gunner_move_stand", "gunner_move_walk",
            "gunner_move_run", "gunner_move_attack_chain",
            "gunner_move_attack_grenade", NULL, "gunner_move_pain1",
            "gunner_move_pain2", "gunner_move_pain3", "gunner_move_death", NULL,
            Q2M_ATTACK_BULLET, Q2M_ATTACK_GRENADE, 3, 50, 600),
    MONSTER("monster_hover", "models/monsters/hover/tris.md2", NULL, Q2M_HOVER,
            Q2M_FLY, BOUNDS(-24, -24, -24, 24, 24, 32), 240, -100, 150, 1, 0,
            10, Q2M_HAS_RANGED | Q2M_SIDESTEPS, "hover_move_stand",
            "hover_move_stand", "hover_move_walk", "hover_move_run",
            "hover_move_start_attack", "hover_move_start_attack2", NULL,
            "hover_move_pain1", "hover_move_pain2", "hover_move_pain3",
            "hover_move_death1", NULL, Q2M_ATTACK_BLASTER, Q2M_ATTACK_BLASTER,
            1, 1, 1000),
    MONSTER(
        "monster_jorg", "models/monsters/boss3/jorg/tris.md2", NULL, Q2M_JORG,
        Q2M_WALK, BOUNDS(-80, -80, 0, 80, 80, 140), 3000, -2000, 1000, 1, 0, 20,
        Q2M_HAS_RANGED | Q2M_BOSS | Q2M_METALLIC | Q2M_EXPLODES,
        "jorg_move_stand", "jorg_move_stand", "jorg_move_walk", "jorg_move_run",
        "jorg_move_start_attack1", "jorg_move_attack2", NULL, "jorg_move_pain1",
        "jorg_move_pain2", "jorg_move_pain3", "jorg_move_death", NULL,
        Q2M_ATTACK_BULLET, Q2M_ATTACK_BFG, 6, 50, 400),
    MONSTER("monster_makron", "models/monsters/boss3/rider/tris.md2", NULL,
            Q2M_MAKRON, Q2M_WALK, BOUNDS(-30, -30, 0, 30, 30, 90), 3000, -2000,
            500, 1, 0, 20,
            Q2M_HAS_RANGED | Q2M_BOSS | Q2M_METALLIC | Q2M_EXPLODES,
            "makron_move_stand", "makron_move_stand", "makron_move_walk",
            "makron_move_run", "makron_move_attack3", "makron_move_attack4",
            NULL, "makron_move_pain4", "makron_move_pain5", "makron_move_pain6",
            "makron_move_death2", "makron_move_death3", Q2M_ATTACK_BLASTER,
            Q2M_ATTACK_RAIL, 15, 50, 1000),
    MONSTER("monster_medic", "models/monsters/medic/tris.md2", NULL, Q2M_MEDIC,
            Q2M_WALK, BOUNDS(-24, -24, -24, 24, 24, 32), 300, -130, 400, 1, 0,
            20, Q2M_HAS_RANGED | Q2M_DUCKS, "medic_move_stand",
            "medic_move_stand", "medic_move_walk", "medic_move_run",
            "medic_move_attackBlaster", "medic_move_callReinforcements", NULL,
            "medic_move_pain1", "medic_move_pain2", NULL, "medic_move_death",
            NULL, Q2M_ATTACK_BLASTER, Q2M_ATTACK_SUMMON, 2, 0, 1000),
    MONSTER("monster_mutant", "models/monsters/mutant/tris.md2", NULL,
            Q2M_MUTANT, Q2M_WALK, BOUNDS(-32, -32, -24, 32, 32, 48), 300, -120,
            300, 1, 0, 20, Q2M_HAS_MELEE | Q2M_TOUCH_ATTACK | Q2M_JUMPS,
            "mutant_move_stand", "mutant_move_stand", "mutant_move_walk",
            "mutant_move_run", "mutant_move_jump", NULL, "mutant_move_attack",
            "mutant_move_pain1", "mutant_move_pain2", "mutant_move_pain3",
            "mutant_move_death1", "mutant_move_death2", Q2M_ATTACK_HIT,
            Q2M_ATTACK_HIT, 10, 40, 0),
    MONSTER("monster_parasite", "models/monsters/parasite/tris.md2", NULL,
            Q2M_PARASITE, Q2M_WALK, BOUNDS(-16, -16, -24, 16, 16, 24), 175, -50,
            250, 1, 0, 20, Q2M_HAS_RANGED | Q2M_JUMPS, "parasite_move_stand",
            "parasite_move_stand", "parasite_move_walk", "parasite_move_run",
            "parasite_move_drain", NULL, NULL, "parasite_move_pain1", NULL,
            NULL, "parasite_move_death", NULL, Q2M_ATTACK_BEAM, Q2M_ATTACK_NONE,
            5, 0, 0),
    MONSTER("monster_supertank", "models/monsters/boss1/tris.md2", NULL,
            Q2M_SUPERTANK, Q2M_WALK, BOUNDS(-64, -64, 0, 64, 64, 112), 1500,
            -500, 800, 1, 0, 20,
            Q2M_HAS_RANGED | Q2M_BOSS | Q2M_METALLIC | Q2M_EXPLODES,
            "supertank_move_stand", "supertank_move_stand",
            "supertank_move_forward", "supertank_move_run",
            "supertank_move_attack1", "supertank_move_attack2", NULL,
            "supertank_move_pain1", "supertank_move_pain2",
            "supertank_move_pain3", "supertank_move_death", NULL,
            Q2M_ATTACK_ROCKET, Q2M_ATTACK_BULLET, 50, 6, 500),
    MONSTER("monster_tank", "models/monsters/tank/tris.md2", NULL, Q2M_TANK,
            Q2M_WALK, BOUNDS(-32, -32, -16, 32, 32, 72), 750, -200, 500, 1, 0,
            20, Q2M_HAS_RANGED | Q2M_METALLIC | Q2M_BLIND_FIRE,
            "tank_move_stand", "tank_move_stand", "tank_move_walk",
            "tank_move_start_run", "tank_move_attack_blast",
            "tank_move_attack_pre_rocket", NULL, "tank_move_pain1",
            "tank_move_pain2", "tank_move_pain3", "tank_move_death", NULL,
            Q2M_ATTACK_BLASTER, Q2M_ATTACK_ROCKET, 30, 50, 650),
    MONSTER("monster_tank_commander", "models/monsters/tank/tris.md2", NULL,
            Q2M_TANK_COMMANDER, Q2M_WALK, BOUNDS(-32, -32, -16, 32, 32, 72),
            1000, -225, 500, 1, 0, 20,
            Q2M_HAS_RANGED | Q2M_METALLIC | Q2M_BLIND_FIRE, "tank_move_stand",
            "tank_move_stand", "tank_move_walk", "tank_move_start_run",
            "tank_move_attack_blast", "tank_move_attack_pre_rocket", NULL,
            "tank_move_pain1", "tank_move_pain2", "tank_move_pain3",
            "tank_move_death", NULL, Q2M_ATTACK_BLASTER, Q2M_ATTACK_ROCKET, 30,
            50, 650),
    MONSTER("monster_boss2", "models/monsters/boss2/tris.md2", NULL, Q2M_BOSS2,
            Q2M_FLY, BOUNDS(-56, -56, 0, 56, 56, 80), 2000, -200, 1000, 1, 0,
            20, Q2M_HAS_RANGED | Q2M_BOSS | Q2M_METALLIC | Q2M_EXPLODES,
            "boss2_move_stand", "boss2_move_stand", "boss2_move_walk",
            "boss2_move_run", "boss2_move_attack_pre_mg",
            "boss2_move_attack_rocket", NULL, "boss2_move_pain_light",
            "boss2_move_pain_heavy", NULL, "boss2_move_death", NULL,
            Q2M_ATTACK_BULLET, Q2M_ATTACK_ROCKET, 6, 50, 500),
    MONSTER("misc_actor", "players/male/tris.md2", NULL, Q2M_ACTOR, Q2M_WALK,
            BOUNDS(-16, -16, -24, 16, 16, 32), 100, -80, 200, 1, 0, 20,
            Q2M_HAS_RANGED | Q2M_GOOD_GUY, "actor_move_stand",
            "actor_move_stand", "actor_move_walk", "actor_move_run",
            "actor_move_attack", NULL, NULL, "actor_move_pain1",
            "actor_move_pain2", "actor_move_pain3", "actor_move_death1",
            "actor_move_death2", Q2M_ATTACK_BULLET, Q2M_ATTACK_NONE, 3, 0, 0),
    MONSTER("misc_insane", "models/monsters/insane/tris.md2", NULL, Q2M_INSANE,
            Q2M_WALK, BOUNDS(-16, -16, -24, 16, 16, 32), 100, -50, 300, 1, 0,
            20, Q2M_GOOD_GUY, "insane_move_stand_normal",
            "insane_move_stand_normal", "insane_move_walk_normal",
            "insane_move_run_normal", NULL, NULL, NULL,
            "insane_move_stand_pain", "insane_move_crawl_pain", NULL,
            "insane_move_stand_death", "insane_move_crawl_death",
            Q2M_ATTACK_NONE, Q2M_ATTACK_NONE, 0, 0, 0),
    MONSTER("monster_gekk", "models/monsters/gekk/tris.md2",
            "missionpacks/xatrix-gekk:gekkMoves", Q2M_GEKK, Q2M_WALK,
            BOUNDS(-24, -24, -24, 24, 24, 24), 125, -30, 300, 1, 0, 20,
            Q2M_HAS_MELEE | Q2M_HAS_RANGED | Q2M_TOUCH_ATTACK | Q2M_JUMPS,
            "gekk_move_stand", "gekk_move_stand", "gekk_move_walk",
            "gekk_move_run", "gekk_move_spit", "gekk_move_leapatk",
            "gekk_move_attack1", "gekk_move_pain", "gekk_move_pain1",
            "gekk_move_pain2", "gekk_move_death1", "gekk_move_death3",
            Q2M_ATTACK_BLASTER, Q2M_ATTACK_HIT, 10, 15, 550),
    MONSTER("monster_fixbot", "models/monsters/fixbot/tris.md2",
            "missionpacks/xatrix-fixbot:fixbotMoves", Q2M_FIXBOT, Q2M_FLY,
            BOUNDS(-32, -32, -24, 32, 32, 24), 150, 0, 150, 1, 0, 10,
            Q2M_HAS_RANGED, "fixbot_move_stand", "fixbot_move_stand",
            "fixbot_move_walk", "fixbot_move_run", "fixbot_move_start_attack",
            "fixbot_move_laserattack", NULL, "fixbot_move_paina",
            "fixbot_move_painb", "fixbot_move_pain3", "fixbot_move_death1",
            NULL, Q2M_ATTACK_BLASTER, Q2M_ATTACK_BEAM, 15, 15, 1000),
    MONSTER("monster_gladb", "models/monsters/gladb/tris.md2",
            "missionpacks/xatrix-gladb:gladbMoves", Q2M_GLADB, Q2M_WALK,
            BOUNDS(-32, -32, -24, 32, 32, 64), 800, -175, 350, 1, 0, 20,
            Q2M_HAS_MELEE | Q2M_HAS_RANGED | Q2M_POWER_SHIELD,
            "gladb_move_stand", "gladb_move_stand", "gladb_move_walk",
            "gladb_move_run", "gladb_move_attack_gun", NULL,
            "gladb_move_attack_melee", "gladb_move_pain", "gladb_move_pain_air",
            NULL, "gladb_move_death", NULL, Q2M_ATTACK_BLASTER, Q2M_ATTACK_HIT,
            100, 20, 725),
    MONSTER("monster_boss5", "models/monsters/boss5/tris.md2",
            "missionpacks/xatrix-boss5:boss5Moves", Q2M_BOSS5, Q2M_WALK,
            BOUNDS(-64, -64, 0, 64, 64, 112), 1500, -500, 800, 1, 0, 20,
            Q2M_HAS_RANGED | Q2M_BOSS | Q2M_METALLIC | Q2M_POWER_SHIELD |
                Q2M_EXPLODES,
            "boss5_move_stand", "boss5_move_stand", "boss5_move_forward",
            "boss5_move_run", "boss5_move_attack1", "boss5_move_attack2", NULL,
            "boss5_move_pain1", "boss5_move_pain2", "boss5_move_pain3",
            "boss5_move_death", NULL, Q2M_ATTACK_ROCKET, Q2M_ATTACK_BULLET, 50,
            6, 500),
    MONSTER("monster_chick_heat", "models/monsters/bitch2/tris.md2", NULL,
            Q2M_CHICK_HEAT, Q2M_WALK, BOUNDS(-16, -16, 0, 16, 16, 56), 175, -70,
            200, 1, 0, 20,
            Q2M_HAS_MELEE | Q2M_HAS_RANGED | Q2M_BLIND_FIRE | Q2M_DUCKS,
            "chick_move_stand", "chick_move_stand", "chick_move_walk",
            "chick_move_run", "chick_move_start_attack1", NULL,
            "chick_move_start_slash", "chick_move_pain1", "chick_move_pain2",
            "chick_move_pain3", "chick_move_death1", "chick_move_death2",
            Q2M_ATTACK_HEAT, Q2M_ATTACK_HIT, 50, 10, 500),
    MONSTER("monster_soldier_ripper", "models/monsters/soldierh/tris.md2",
            "missionpacks/xatrix-soldierh:soldierhMoves", Q2M_SOLDIER_RIPPER,
            Q2M_WALK, BOUNDS(-16, -16, -24, 16, 16, 32), 50, -30, 100, 1, 0, 20,
            Q2M_HAS_RANGED | Q2M_BLIND_FIRE | Q2M_DUCKS, "soldierh_move_stand3",
            "soldierh_move_stand1", "soldierh_move_walk1", "soldierh_move_run",
            "soldierh_move_attack1", "soldierh_move_attack2", NULL,
            "soldierh_move_pain1", "soldierh_move_pain2", "soldierh_move_pain3",
            "soldierh_move_death1", "soldierh_move_death2", Q2M_ATTACK_ION,
            Q2M_ATTACK_NONE, 5, 0, 600),
    MONSTER("monster_soldier_hypergun", "models/monsters/soldierh/tris.md2",
            "missionpacks/xatrix-soldierh:soldierhMoves", Q2M_SOLDIER_HYPER,
            Q2M_WALK, BOUNDS(-16, -16, -24, 16, 16, 32), 60, -30, 100, 1, 0, 20,
            Q2M_HAS_RANGED | Q2M_DUCKS, "soldierh_move_stand3",
            "soldierh_move_stand1", "soldierh_move_walk1", "soldierh_move_run",
            "soldierh_move_attack1", "soldierh_move_attack2", NULL,
            "soldierh_move_pain1", "soldierh_move_pain2", "soldierh_move_pain3",
            "soldierh_move_death1", "soldierh_move_death2",
            Q2M_ATTACK_BLUE_BOLT, Q2M_ATTACK_NONE, 1, 0, 600),
    MONSTER("monster_soldier_lasergun", "models/monsters/soldierh/tris.md2",
            "missionpacks/xatrix-soldierh:soldierhMoves", Q2M_SOLDIER_LASER,
            Q2M_WALK, BOUNDS(-16, -16, -24, 16, 16, 32), 70, -30, 100, 1, 0, 20,
            Q2M_HAS_RANGED | Q2M_DUCKS, "soldierh_move_stand3",
            "soldierh_move_stand1", "soldierh_move_walk1", "soldierh_move_run",
            "soldierh_move_attack1", "soldierh_move_attack2", NULL,
            "soldierh_move_pain1", "soldierh_move_pain2", "soldierh_move_pain3",
            "soldierh_move_death1", "soldierh_move_death2", Q2M_ATTACK_BEAM,
            Q2M_ATTACK_NONE, 1, 0, 1000),
    MONSTER("monster_stalker", "models/monsters/stalker/tris.md2",
            "missionpacks/rogue-stalker:stalkerMoves", Q2M_STALKER, Q2M_WALK,
            BOUNDS(-28, -28, -18, 28, 28, 18), 250, -50, 250, 1, 0, 20,
            Q2M_HAS_MELEE | Q2M_HAS_RANGED | Q2M_JUMPS | Q2M_REGENERATES,
            "stalker_move_stand", "stalker_move_stand", "stalker_move_walk",
            "stalker_move_run", "stalker_move_shoot", NULL,
            "stalker_move_swing_l", "stalker_move_pain", NULL, NULL,
            "stalker_move_death", NULL, Q2M_ATTACK_GREEN_BOLT, Q2M_ATTACK_HIT,
            15, 5, 800),
    MONSTER("monster_kamikaze", "models/monsters/flyer/tris.md2",
            "missionpacks/rogue-flyer:flyerMoves", Q2M_KAMIKAZE, Q2M_FLY,
            BOUNDS(-16, -16, -24, 16, 16, 16), 50, 0, 100, 1, 0, 10,
            Q2M_TOUCH_ATTACK | Q2M_EXPLODES | Q2M_DO_NOT_COUNT,
            "flyer_move_kamikaze", "flyer_move_stand", "flyer_move_walk",
            "flyer_move_kamikaze", "flyer_move_kamikaze", NULL, NULL,
            "flyer_move_pain1", "flyer_move_pain2", "flyer_move_pain3",
            "flyer_move_pain1", NULL, Q2M_ATTACK_NONE, Q2M_ATTACK_NONE, 0, 0,
            0),
    MONSTER("monster_daedalus", "models/monsters/hover/tris.md2",
            "missionpacks/rogue-hover:hoverMoves", Q2M_DAEDALUS, Q2M_FLY,
            BOUNDS(-24, -24, -24, 24, 24, 32), 450, -100, 225, 1, 0, 25,
            Q2M_HAS_RANGED | Q2M_SIDESTEPS, "hover_move_stand",
            "hover_move_stand", "hover_move_walk", "hover_move_run",
            "hover_move_start_attack", "hover_move_start_attack2", NULL,
            "hover_move_pain1", "hover_move_pain2", "hover_move_pain3",
            "hover_move_death1", NULL, Q2M_ATTACK_GREEN_BOLT,
            Q2M_ATTACK_GREEN_BOLT, 1, 1, 1000),
    MONSTER("monster_turret", "models/monsters/turret/tris.md2",
            "missionpacks/rogue-turret:turretMoves", Q2M_TURRET, Q2M_STATIONARY,
            BOUNDS(-12, -12, -12, 12, 12, 12), 240, -100, 250, 1, 0, 45,
            Q2M_HAS_RANGED | Q2M_BLIND_FIRE | Q2M_METALLIC, "turret_move_stand",
            "turret_move_stand", "turret_move_stand", "turret_move_run",
            "turret_move_fire", "turret_move_fire_blind", NULL, NULL, NULL,
            NULL, "turret_move_stand", NULL, Q2M_ATTACK_ROCKET,
            Q2M_ATTACK_BULLET, 50, 3, 650),
    MONSTER("monster_carrier", "models/monsters/carrier/tris.md2",
            "missionpacks/rogue-carrier:carrierMoves", Q2M_CARRIER, Q2M_FLY,
            BOUNDS(-56, -56, -44, 56, 56, 44), 2000, -200, 1000, 1, 0, 15,
            Q2M_HAS_RANGED | Q2M_BLIND_FIRE | Q2M_BOSS | Q2M_EXPLODES |
                Q2M_NO_GIB,
            "carrier_move_stand", "carrier_move_stand", "carrier_move_walk",
            "carrier_move_run", "carrier_move_attack_pre_mg",
            "carrier_move_attack_rocket", NULL, "carrier_move_pain_light",
            "carrier_move_pain_heavy", NULL, "carrier_move_death", NULL,
            Q2M_ATTACK_BULLET, Q2M_ATTACK_ROCKET, 6, 50, 750),
    MONSTER("monster_medic_commander", "models/monsters/medic/tris.md2",
            "missionpacks/rogue-medic:medicMoves", Q2M_MEDIC_COMMANDER,
            Q2M_WALK, BOUNDS(-24, -24, -24, 24, 24, 32), 600, -130, 600, 1, 0,
            40, Q2M_HAS_RANGED | Q2M_DUCKS | Q2M_POWER_SHIELD,
            "medic_move_stand", "medic_move_stand", "medic_move_walk",
            "medic_move_run", "medic_move_attackBlaster",
            "medic_move_callReinforcements", NULL, "medic_move_pain1",
            "medic_move_pain2", NULL, "medic_move_death", NULL,
            Q2M_ATTACK_GREEN_BOLT, Q2M_ATTACK_SUMMON, 3, 0, 1000),
    MONSTER("monster_widow", "models/monsters/blackwidow/tris.md2",
            "missionpacks/rogue-widow:widowMoves", Q2M_WIDOW, Q2M_WALK,
            BOUNDS(-40, -40, 0, 40, 40, 144), 2000, -5000, 1500, 1, 0, 30,
            Q2M_HAS_MELEE | Q2M_HAS_RANGED | Q2M_BOSS | Q2M_NO_GIB |
                Q2M_POWER_SHIELD,
            "widow_move_stand", "widow_move_stand", "widow_move_walk",
            "widow_move_run", "widow_move_attack_pre_blaster",
            "widow_move_attack_pre_rail", "widow_move_attack_kick",
            "widow_move_pain_light", "widow_move_pain_heavy", NULL,
            "widow_move_death", NULL, Q2M_ATTACK_GREEN_BOLT, Q2M_ATTACK_RAIL,
            10, 50, 1000),
    MONSTER("monster_widow2", "models/monsters/blackwidow2/tris.md2",
            "missionpacks/rogue-widow2:widow2Moves", Q2M_WIDOW2, Q2M_WALK,
            BOUNDS(-70, -70, 0, 70, 70, 144), 2800, -900, 2500, 1, 0, 30,
            Q2M_HAS_MELEE | Q2M_HAS_RANGED | Q2M_BOSS | Q2M_NO_GIB |
                Q2M_POWER_SHIELD,
            "widow2_move_stand", "widow2_move_stand", "widow2_move_walk",
            "widow2_move_run", "widow2_move_attack_pre_beam",
            "widow2_move_attack_disrupt", "widow2_move_tongs",
            "widow2_move_pain", NULL, NULL, "widow2_move_death", NULL,
            Q2M_ATTACK_BEAM, Q2M_ATTACK_TRACKER, 15, 20, 0),
    MONSTER("monster_arachnid", "models/monsters/arachnid/tris.md2",
            "rerelease/arachnid:arachnidMoves", Q2M_ARACHNID, Q2M_WALK,
            BOUNDS(-48, -48, -20, 48, 48, 48), 1000, -200, 450, 1, 0, 20,
            Q2M_HAS_MELEE | Q2M_HAS_RANGED, "arachnid_move_stand",
            "arachnid_move_stand", "arachnid_move_walk", "arachnid_move_run",
            "arachnid_attack1", "arachnid_attack_up1", "arachnid_melee",
            "arachnid_move_pain1", "arachnid_move_pain2", NULL,
            "arachnid_move_death", NULL, Q2M_ATTACK_RAIL, Q2M_ATTACK_HIT, 35,
            15, 0),
    MONSTER("monster_guardian", "models/monsters/guardian/tris.md2",
            "rerelease/guardian:guardianMoves", Q2M_GUARDIAN, Q2M_WALK,
            BOUNDS(-96, -96, -66, 96, 96, 62), 2500, -200, 850, 1, 0, 20,
            Q2M_HAS_MELEE | Q2M_HAS_RANGED | Q2M_BOSS | Q2M_NO_GIB |
                Q2M_METALLIC,
            "guardian_move_stand", "guardian_move_stand", "guardian_move_walk",
            "guardian_move_run", "guardian_move_atk1_in",
            "guardian_move_atk2_in", "guardian_move_kick",
            "guardian_move_pain1", NULL, NULL, "guardian_move_death", NULL,
            Q2M_ATTACK_BLASTER, Q2M_ATTACK_BEAM, 2, 25, 1000),
    MONSTER("monster_guncmdr", "models/monsters/gunner/tris.md2",
            "rerelease/guncmdr:guncmdrMoves", Q2M_GUN_COMMANDER, Q2M_WALK,
            BOUNDS(-16, -16, -24, 16, 16, 36), 325, -175, 255, 1.15f, 37, 20,
            Q2M_HAS_MELEE | Q2M_HAS_RANGED | Q2M_DUCKS | Q2M_SIDESTEPS |
                Q2M_JUMPS,
            "guncmdr_move_stand", "guncmdr_move_stand", "guncmdr_move_walk",
            "guncmdr_move_run", "guncmdr_move_attack_chain",
            "guncmdr_move_attack_mortar", "guncmdr_move_attack_kick",
            "guncmdr_move_pain1", "guncmdr_move_pain2", "guncmdr_move_pain3",
            "guncmdr_move_death1", "guncmdr_move_death2", Q2M_ATTACK_FLECHETTE,
            Q2M_ATTACK_GRENADE, 4, 50, 800),
    MONSTER("monster_shambler", "models/monsters/shambler/tris.md2",
            "rerelease/shambler:shamblerMoves", Q2M_SHAMBLER, Q2M_WALK,
            BOUNDS(-32, -32, -24, 32, 32, 64), 600, -60, 500, 1, 0, 20,
            Q2M_HAS_MELEE | Q2M_HAS_RANGED, "shambler_move_stand",
            "shambler_move_stand", "shambler_move_walk", "shambler_move_run",
            "shambler_attack_magic", "shambler_attack_smash",
            "shambler_attack_swingl", "shambler_move_pain", NULL, NULL,
            "shambler_move_death", NULL, Q2M_ATTACK_BEAM, Q2M_ATTACK_HIT, 10,
            40, 0),
    MONSTER("turret_driver", "models/monsters/infantry/tris.md2", NULL,
            Q2M_TURRET_DRIVER, Q2M_STATIONARY,
            BOUNDS(-16, -16, -24, 16, 16, 32), 100, 0, 200, 1, 24, 20,
            Q2M_HAS_RANGED | Q2M_DO_NOT_COUNT, "infantry_move_stand",
            "infantry_move_stand", "infantry_move_walk", "infantry_move_run",
            "infantry_move_attack1", NULL, NULL, "infantry_move_pain1",
            "infantry_move_pain2", NULL, "infantry_move_death1", NULL,
            Q2M_ATTACK_BULLET, Q2M_ATTACK_NONE, 3, 0, 0),
    MONSTER("monster_tank_stand", "models/monsters/tank/tris.md2",
            "rerelease/tank:tankMoves", Q2M_TANK_STAND, Q2M_STATIONARY,
            BOUNDS(-32, -32, -16, 32, 32, 64), 1, 0, 750, 1, 0, 0,
            Q2M_NO_GIB | Q2M_DO_NOT_COUNT, "tank_move_stand", "tank_move_stand",
            "tank_move_stand", "tank_move_stand", NULL, NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, Q2M_ATTACK_NONE, Q2M_ATTACK_NONE, 0, 0, 0),
    MONSTER("monster_boss3_stand", "models/monsters/boss3/rider/tris.md2",
            "base/boss32:boss32Moves", Q2M_BOSS3_STAND, Q2M_STATIONARY,
            BOUNDS(-32, -32, 0, 32, 32, 90), 1, 0, 500, 1, 0, 0,
            Q2M_NO_GIB | Q2M_DO_NOT_COUNT, "makron_move_stand",
            "makron_move_stand", "makron_move_stand", "makron_move_stand", NULL,
            NULL, NULL, NULL, NULL, NULL, NULL, NULL, Q2M_ATTACK_NONE,
            Q2M_ATTACK_NONE, 0, 0, 0),
};

static const q2m_definition rerelease_jorg = MONSTER(
    "monster_jorg", "models/monsters/boss3/jorg/tris.md2",
    "rerelease/boss31:boss31Moves", Q2M_JORG, Q2M_WALK,
    BOUNDS(-80, -80, 0, 80, 80, 140), 8000, -2000, 1000, 1, 0, 20,
    Q2M_HAS_RANGED | Q2M_BOSS | Q2M_METALLIC | Q2M_EXPLODES, "jorg_move_stand",
    "jorg_move_stand", "jorg_move_walk", "jorg_move_run",
    "jorg_move_start_attack1", "jorg_move_attack2", NULL, "jorg_move_pain1",
    "jorg_move_pain2", "jorg_move_pain3", "jorg_move_death", NULL,
    Q2M_ATTACK_BULLET, Q2M_ATTACK_BFG, 6, 50, 400);

static const q2m_definition rerelease_gunner =
    MONSTER("monster_gunner", "models/monsters/gunner/tris.md2",
            "rerelease/gunner:gunnerMoves", Q2M_GUNNER, Q2M_WALK,
            BOUNDS(-16, -16, -24, 16, 16, 32), 175, -70, 200, 1.15f, 0, 20,
            Q2M_HAS_RANGED | Q2M_DUCKS | Q2M_SIDESTEPS | Q2M_JUMPS,
            "gunner_move_stand", "gunner_move_stand", "gunner_move_walk",
            "gunner_move_run", "gunner_move_attack_chain",
            "gunner_move_attack_grenade", NULL, "gunner_move_pain1",
            "gunner_move_pain2", "gunner_move_pain3", "gunner_move_death", NULL,
            Q2M_ATTACK_BULLET, Q2M_ATTACK_GRENADE, 3, 50, 600);

static const q2m_definition rerelease_gladb = MONSTER(
    "monster_gladb", "models/monsters/gladb/tris.md2",
    "rerelease/gladiator:gladiatorMoves", Q2M_GLADB, Q2M_WALK,
    BOUNDS(-32, -32, -24, 32, 32, 64), 250, -175, 350, 1, 0, 20,
    Q2M_HAS_MELEE | Q2M_HAS_RANGED | Q2M_POWER_SHIELD, "gladiator_move_stand",
    "gladiator_move_stand", "gladiator_move_walk", "gladiator_move_run",
    "gladb_move_attack_gun", NULL, "gladiator_move_attack_melee",
    "gladiator_move_pain", "gladiator_move_pain_air", NULL,
    "gladiator_move_death", NULL, Q2M_ATTACK_PLASMA, Q2M_ATTACK_HIT, 100, 20,
    725);

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
  const char *key = definition->move_set;
  if (key == NULL)
    key = base_move_set(game, definition->species);
  return q2m_moves_named(key);
}

const q2m_move *q2m_move_named(const struct qa_q2_monster *monster,
                               const char *name) {
  if (monster == NULL || monster->move_set == NULL || name == NULL)
    return NULL;
  for (size_t i = 0; i < monster->move_set->move_count; ++i)
    if (strcmp(monster->move_set->moves[i].name, name) == 0)
      return &monster->move_set->moves[i];
  return NULL;
}

#undef MONSTER
#undef BOUNDS
