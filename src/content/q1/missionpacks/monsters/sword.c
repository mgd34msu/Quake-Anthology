/* Port of src/content/q1/missionpacks/monsters/sword.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/sword.h"

#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "qa/content/q1/missionpacks/monsters/tables/invis_sw.h"
#include "monsters_internal.h"

static void sword_pause(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  double delay = monster->entity->delay;
  monster->entity->delay = 0;
  qa_q1mpm_monster_set_next_frame(monster, "sword_run1");
  qa_q1mpm_fields_set(monster->entity, "sword:awakened", "1");
  qa_q1mpm_monster_delay(monster, delay);
}
static void sword_run1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->entity->effects = 8;
  qa_q1mpm_monster_ai(monster, QA_Q1MPM_AI_RUN, 14);
}
static void sword_atk1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->game->sound(monster->game, monster->entity, "knight/sword1.wav", QA_Q1MPM_CH("auto"), 1, 1);
  qa_q1mpm_monster_ai(monster, QA_Q1MPM_AI_CHARGE, 14);
}
static void sword_die7(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_event event = {0};
  event.kind = "sound";
  event.actor = QA_Q1MPM_SOME(monster->entity->actor.id);
  event.path = "player/axhit2.wav";
  event.channel = QA_Q1MPM_CH("weapon");
  event.volume = 0.5;
  event.attenuation = 1;
  monster->game->host->emit(monster->game->host->ctx, &event);
}
static void spawn(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  if (monster->entity->delay == 0) monster->entity->delay = 10;
  qa_q1mpm_monster_spawn_default(monster);
}
static void found(void *ctx, qa_q1mpm_mission_monster *monster, qa_actor_id target) {
  (void)ctx;
  qa_q1mpm_monster_found_default(monster, target);
  if (qa_q1mpm_actor_number(monster->entity, "sword:awakened") == 0)
    qa_q1mpm_monster_set_next_frame(monster, "sword_pause");
}
static void pain(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker, double damage) {
  (void)ctx; (void)attacker; (void)damage;
  if (qa_q1mpm_actor_number(monster->entity, "sword:pain-disabled") != 0) return;
  qa_q1mpm_fields_set(monster->entity, "sword:pain-disabled", "1");
  qa_q1mpm_fields_set(monster->entity, "sword:awakened", "1");
  monster->entity->delay = 0;
  qa_q1mpm_monster_set_next_frame(monster, "sword_run1");
  qa_q1mpm_monster_delay(monster, 0.1);
}
static void melee_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_monster_play(monster, "sword_atk1");
}
static void die(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker) {
  (void)ctx; (void)attacker;
  monster->entity->effects = 0;
  qa_q1mpm_monster_play(monster, monster->game->host->random(monster->game->host->ctx) < 0.5
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
