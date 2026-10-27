/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/ai_context.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void qa_bot_cvar_register_bots_a(qa_bot_cvar_bots_a *c, const char *name, const char *def,
                                 const char *(*lookup)(void *, const char *), void *lctx) {
  snprintf(c->name, sizeof c->name, "%s", name);
  c->registered = true;
  const char *v = lookup ? lookup(lctx, name) : NULL;
  if (!v)
    v = def;
  snprintf(c->value, sizeof c->value, "%s", v);
  c->numeric_value = qa_native_atof(v);
  c->integer_value = qa_native_atoi(v);
}
void qa_bot_cvar_update_bots_a(qa_bot_cvar_bots_a *c, const char *(*lookup)(void *, const char *),
                               void *lctx) {
  if (!c->registered || !lookup)
    return;
  const char *v = lookup(lctx, c->name);
  if (!v)
    return;
  if (strcmp(v, c->value) != 0)
    c->modification_count++;
  snprintf(c->value, sizeof c->value, "%s", v);
  c->numeric_value = qa_native_atof(v);
  c->integer_value = qa_native_atoi(v);
}
void qa_bot_cvar_write_integer(qa_bot_cvar_bots_a *c, int64_t v) { c->integer_value = (int32_t)v; }
bool qa_game_ai_context_bots_a_init(qa_game_ai_context_bots_a *c, int64_t mc) {
  memset(c, 0, sizeof *c);
  if (!qa_bot_entity_observations_init(&c->observations, 1024))
    return false;
  if (!qa_bot_state_store_bots_a_init(&c->states, mc))
    return false;
  c->maxclients = mc;
  return true;
}
void qa_game_ai_context_bots_a_destroy(qa_game_ai_context_bots_a *c) {
  qa_bot_entity_observations_destroy(&c->observations);
  qa_bot_state_store_bots_a_destroy(&c->states);
  for (size_t i = 0; i < c->node_switch_count; i++)
    free(c->node_switches[i]);
}
double qa_game_ai_context_random(qa_game_ai_context_bots_a *c) {
  return c->random_unit ? c->random_unit(c->host_ctx) : 0.0;
}
void qa_game_ai_context_reset_state(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s) {
  if (c->reset_state_hook) {
    c->reset_state_hook(c->host_ctx, s);
    return;
  }
  int64_t client = s->client;
  memset(s, 0, sizeof *s);
  s->client = client;
  s->enemy = -1;
}
void qa_bot_reset_state_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s) {
  qa_game_ai_context_reset_state(c, s);
}
