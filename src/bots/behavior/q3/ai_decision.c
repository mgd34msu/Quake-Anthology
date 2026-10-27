/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/ai_decision.h"
#include "qa/bots/behavior/q3/ai_combat.h"
#include "qa/bots/behavior/q3/ai_definitions.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void qa_bot_reset_node_switches_bots_a(qa_game_ai_context_bots_a *c) {
  for (size_t i = 0; i < c->node_switch_count; i++)
    free(c->node_switches[i]);
  c->node_switch_count = 0;
}
void qa_bot_record_node_switch_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s,
                                      const char *node, const char *detail, const char *reason) {
  (void)s;
  if (c->node_switch_count >= QA_MAX_NODESWITCHES)
    return;
  char buf[512];
  snprintf(buf, sizeof buf, "%s %s %s", node ? node : "", detail ? detail : "",
           reason ? reason : "");
  size_t n = strlen(buf) + 1;
  char *copy = malloc(n);
  if (!copy)
    return;
  memcpy(copy, buf, n);
  c->node_switches[c->node_switch_count++] = copy;
}
void qa_bot_dump_node_switches_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s) {
  (void)s;
  if (!c->print)
    return;
  for (size_t i = 0; i < c->node_switch_count; i++)
    c->print(c->host_ctx, c->node_switches[i]);
}
bool qa_bot_reached_goal_bots_a(qa_bot_state_bots_a *s, qa_bot_goal g) {
  return qa_touching_goal(s->origin, g);
}
static void enter_node(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s, qa_ai_node_bots_a node,
                       const char *name, const char *reason) {
  s->ai_node = node;
  qa_bot_record_node_switch_bots_a(c, s, name, "", reason);
}
void qa_ai_enter_intermission_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s,
                                     const char *r) {
  enter_node(c, s, QA_AI_NODE_INTERMISSION, "intermission", r);
}
void qa_ai_enter_observer_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s,
                                 const char *r) {
  enter_node(c, s, QA_AI_NODE_OBSERVER, "observer", r);
}
void qa_ai_enter_stand_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s, const char *r) {
  enter_node(c, s, QA_AI_NODE_STAND, "stand", r);
  s->stand_time = (float)c->time;
}
void qa_ai_enter_respawn_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s,
                                const char *r) {
  enter_node(c, s, QA_AI_NODE_RESPAWN, "respawn", r);
}
void qa_ai_enter_seek_nbg_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s,
                                 const char *r) {
  enter_node(c, s, QA_AI_NODE_SEEK_NBG, "seek-nbg", r);
}
void qa_ai_enter_seek_ltg_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s,
                                 const char *r) {
  enter_node(c, s, QA_AI_NODE_SEEK_LTG, "seek-ltg", r);
}
void qa_ai_enter_battle_fight_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s,
                                     const char *r) {
  enter_node(c, s, QA_AI_NODE_BATTLE_FIGHT, "battle-fight", r);
}
void qa_ai_enter_battle_chase_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s,
                                     const char *r) {
  enter_node(c, s, QA_AI_NODE_BATTLE_CHASE, "battle-chase", r);
}
void qa_ai_enter_battle_retreat_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s,
                                       const char *r) {
  enter_node(c, s, QA_AI_NODE_BATTLE_RETREAT, "battle-retreat", r);
}
void qa_ai_enter_battle_nbg_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s,
                                   const char *r) {
  enter_node(c, s, QA_AI_NODE_BATTLE_NBG, "battle-nbg", r);
}
bool qa_ai_node_intermission_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s) {
  return qa_bot_intermission_bots_a(c, s);
}
bool qa_ai_node_observer_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s) {
  (void)c;
  return qa_bot_is_observer_bots_a(s);
}
bool qa_ai_node_stand_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s) {
  if (qa_bot_intermission_bots_a(c, s)) {
    qa_ai_enter_intermission_bots_a(c, s, "stand->intermission");
    return false;
  }
  if (qa_bot_is_dead_bots_a(s)) {
    qa_ai_enter_respawn_bots_a(c, s, "stand->respawn");
    return false;
  }
  return true;
}
bool qa_ai_node_respawn_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s) {
  if (!qa_bot_is_dead_bots_a(s)) {
    qa_ai_enter_stand_bots_a(c, s, "respawn->stand");
    return false;
  }
  return true;
}
bool qa_run_ai_node_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s,
                           bool (*seek_hook)(void *, qa_game_ai_context_bots_a *,
                                             qa_bot_state_bots_a *),
                           void *hc) {
  switch (s->ai_node) {
  case QA_AI_NODE_INTERMISSION:
    return qa_ai_node_intermission_bots_a(c, s);
  case QA_AI_NODE_OBSERVER:
    return qa_ai_node_observer_bots_a(c, s);
  case QA_AI_NODE_STAND:
    return qa_ai_node_stand_bots_a(c, s);
  case QA_AI_NODE_RESPAWN:
    return qa_ai_node_respawn_bots_a(c, s);
  default:
    return seek_hook ? seek_hook(hc, c, s) : false;
  }
}
