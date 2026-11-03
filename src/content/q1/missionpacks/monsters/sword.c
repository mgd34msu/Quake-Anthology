/* Port of src/content/q1/missionpacks/monsters/sword.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/sword.h"

#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "qa/content/q1/missionpacks/monsters/tables/invis_sw.h"
#include "monsters_internal.h"

static void sword_pause(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  double delay = monster->base.entity->delay;
  monster->base.entity->delay = 0;
  q1mpm_set_next_frame(&monster->base, "sword_run1");
  (void)qa_q1_actor_field_set(monster->base.entity, "sword:awakened", "1");
  qa_q1_base_monster_delay(&monster->base, delay);
}
static void sword_run1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->base.entity->effects = 8;
  qa_q1_base_monster_ai(&monster->base, QA_Q1_AI_RUN, 14);
}
static void sword_atk1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_sound(monster->base.game, monster->base.entity->actor.id, "knight/sword1.wav", "auto", 1, 1);
  qa_q1_base_monster_ai(&monster->base, QA_Q1_AI_CHARGE, 14);
}
static void sword_die7(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  q1mpm_emit_sound(monster->base.game, monster->base.entity->actor.id, "player/axhit2.wav", "weapon", 0.5, 1);
}
static void spawn(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  if (monster->base.entity->delay == 0) monster->base.entity->delay = 10;
  qa_q1mpm_monster_spawn_default(monster);
}
static void found(void *ctx, qa_q1mpm_mission_monster *monster, qa_actor_id target) {
  (void)ctx;
  qa_q1mpm_monster_found_default(monster, target);
  if (qa_q1_actor_number(monster->base.entity, "sword:awakened") == 0)
    q1mpm_set_next_frame(&monster->base, "sword_pause");
}
static void pain(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker, double damage) {
  (void)ctx; (void)attacker; (void)damage;
  if (qa_q1_actor_number(monster->base.entity, "sword:pain-disabled") != 0) return;
  (void)qa_q1_actor_field_set(monster->base.entity, "sword:pain-disabled", "1");
  (void)qa_q1_actor_field_set(monster->base.entity, "sword:awakened", "1");
  monster->base.entity->delay = 0;
  q1mpm_set_next_frame(&monster->base, "sword_run1");
  qa_q1_base_monster_delay(&monster->base, 0.1);
}
static void melee_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_base_monster_play(&monster->base, "sword_atk1");
}
static void die(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker) {
  (void)ctx; (void)attacker;
  monster->base.entity->effects = 0;
  qa_q1_base_monster_play(&monster->base, qa_q1_host_random(monster->base.game) < 0.5
                                     ? "sword_die1" : "sword_dieb1");
}
static const char *const classnames[] = {"monster_sword"};
static const qa_q1mpm_named_action actions[] = {
    {"sword_pause", {sword_pause, NULL}},
    {"invis_sw:sword_run1", {sword_run1, NULL}},
    {"invis_sw:sword_atk1", {sword_atk1, NULL}},
    {"invis_sw:sword_die7", {sword_die7, NULL}},
};
const qa_q1mpm_pack_monster_definition qa_q1mpm_sword_definition = {
    .spec = {"sword", NULL, classnames, 1, "sword", NULL, 150, -INFINITY, NULL, 0,
             {{-16, -16, -24}, {16, 16, 40}}, "sword_stand1", "sword_stand1", "sword_run1",
             "knight/ksight.wav", NULL, true, "walk"},
    .frames = {qa_q1mpm_tables_invis_sw_entries, QA_Q1MPM_TABLES_INVIS_SW_COUNT},
    .actions = actions,
    .action_count = sizeof(actions) / sizeof(actions[0]),
    .spawn = {spawn, NULL},
    .pain = pain,
    .die = die,
    .melee = {melee_action, NULL},
    .found = found,
};
