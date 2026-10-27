/* Port of src/content/q3/base/game/save-state.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/save_state.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
static char *dupstr(const char *v) { if (!v) return NULL; size_t n = strlen(v); char *d = malloc(n + 1); if (d) memcpy(d, v, n + 1); return d; }
void qa_q3_graph_state_destroy(qa_q3_graph_state *s) {
  free(s->ownership);
  for (size_t i = 0; i < s->entity_count; i++) { qa_q3_entity_state *e = &s->entities[i]; qa_q3_entity_values_destroy(&e->values);
    free(e->classname); free(e->think); free(e->reached); free(e->blocked); free(e->touch); free(e->use); free(e->pain); free(e->die); }
  free(s->entities);
  for (size_t i = 0; i < s->client_count; i++) { qa_q3_client_state *c = &s->clients[i]; qa_q3_persistant_values_destroy(&c->persistant);
    free(c->events); free(c->event_parms); free(c->persistant_slots); free(c->powerups); free(c->ammo_times); }
  free(s->clients); memset(s, 0, sizeof *s);
}
static double *copy_slots(qa_q3_game_player_slots_mirror *slots, size_t *out) {
  *out = slots->length; double *d = malloc((slots->length ? slots->length : 1) * sizeof *d);
  if (d) for (size_t i = 0; i < slots->length; i++) d[i] = slots->values[i];
  return d;
}
qa_q3_graph_state qa_q3_capture_graph(qa_q3_entity_pool *pool) {
  qa_q3_graph_state s; memset(&s, 0, sizeof s);
  s.num_entities = pool->num_entities; s.max_clients = pool->max_clients;
  s.entity_count = QA_Q3_MAX_GENTITIES; s.entities = calloc(s.entity_count, sizeof *s.entities);
  s.ownership_count = QA_Q3_MAX_GENTITIES; s.ownership = calloc(s.ownership_count, sizeof *s.ownership);
  s.client_count = QA_Q3_MAX_CLIENTS; s.clients = calloc(s.client_count, sizeof *s.clients);
  if (!s.entities || !s.ownership || !s.clients) qa_range_error("out of memory");
  for (size_t i = 0; i < s.entity_count; i++) {
    qa_q3_game_entity *e = qa_q3_entity_pool_get(pool, (double)i); qa_q3_entity_state *st = &s.entities[i];
    s.ownership[i].active = e && qa_q3_game_entity_inuse(e);
    if (!e) continue;
    st->values = qa_q3_capture_entity_values(e); st->network = qa_q3_capture_network_values(&e->s);
    st->pos = e->s.pos; st->apos = e->s.apos; st->sv_flags = e->r.sv_flags; st->single_client = e->r.single_client;
    st->contents = e->r.contents; st->owner_num = e->r.owner_num;
    st->has_client = e->client != NULL;
    st->classname = dupstr(qa_q3_game_entity_classname(e)); st->nextthink = e->nextthink;
    st->has_parent = e->parent != NULL; if (e->parent) st->parent = e->parent->slot;
    st->has_enemy = e->enemy != NULL; if (e->enemy) st->enemy = e->enemy->slot;
    st->think = dupstr(qa_q3_callback_family_capture(&pool->callbacks.think, e->think));
    st->touch = dupstr(qa_q3_callback_family_capture(&pool->callbacks.touch, e->touch));
    st->use = dupstr(qa_q3_callback_family_capture(&pool->callbacks.use, e->use));
  }
  for (size_t i = 0; i < s.client_count; i++) {
    qa_q3_game_client *c = pool->clients[i]; qa_q3_client_state *st = &s.clients[i]; if (!c) continue;
    st->values = qa_q3_capture_client_values(c); st->player = qa_q3_capture_player_values(&c->ps);
    st->persistant = qa_q3_capture_persistant_values(&c->pers); st->command = c->pers.cmd;
    st->team = qa_q3_capture_team_values(&c->pers.team_state); st->session = qa_q3_capture_session_values(&c->sess);
    st->events = copy_slots(&c->ps.events, &st->event_count); st->event_parms = copy_slots(&c->ps.event_parms, &st->event_parm_count);
    st->persistant_slots = copy_slots(&c->ps.persistant, &st->persistant_count);
    st->powerups = copy_slots(&c->ps.powerups, &st->powerup_count); st->ammo_times = copy_slots(&c->ammo_times, &st->ammo_time_count);
    st->has_hook = c->hook != NULL; if (c->hook) st->hook = c->hook->slot;
  }
  return s;
}
void qa_q3_restore_graph(qa_q3_entity_pool *pool, qa_q3_graph_state *state) {
  qa_q3_pool_counts counts = {state->num_entities, state->max_clients}; qa_q3_entity_pool_restore_counts(pool, counts);
  for (size_t i = 0; i < state->entity_count; i++) {
    qa_q3_game_entity *e = qa_q3_entity_pool_get(pool, (double)i); if (!e) continue; qa_q3_entity_state *st = &state->entities[i];
    qa_q3_restore_entity_values(e, &st->values); qa_q3_restore_network_values(&e->s, st->network);
    e->s.pos = st->pos; e->s.apos = st->apos; qa_q3_game_entity_restore_next_think(e, st->nextthink);
    e->think = qa_q3_callback_family_resolve(&pool->callbacks.think, st->think);
    e->touch = qa_q3_callback_family_resolve(&pool->callbacks.touch, st->touch);
    e->use = qa_q3_callback_family_resolve(&pool->callbacks.use, st->use);
  }
  for (size_t i = 0; i < state->client_count; i++) {
    qa_q3_game_client *c = pool->clients[i]; if (!c) continue; qa_q3_client_state *st = &state->clients[i];
    qa_q3_restore_client_values(c, st->values); qa_q3_restore_player_values(&c->ps, st->player);
    qa_q3_restore_persistant_values(&c->pers, &st->persistant); qa_q3_restore_team_values(&c->pers.team_state, st->team);
    qa_q3_restore_session_values(&c->sess, st->session);
  }
}
