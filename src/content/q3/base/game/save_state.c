/* Port of src/content/q3/base/game/save-state.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/save_state.h"
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/base/game/save_reader.h"
#include "qa/content/q3/base/shared/entity_shared.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/persistence/save_image.h"
static char *dupstr(const char *v) { if (!v) return NULL; size_t n = strlen(v); char *d = malloc(n + 1); if (!d) qa_range_error("out of memory"); memcpy(d, v, n + 1); return d; }
void qa_q3_graph_state_destroy(qa_q3_graph_state *s) {
  free(s->ownership);
  for (size_t i = 0; i < s->entity_count; i++) { qa_q3_game_save_entity_state *e = &s->entities[i]; qa_q3_entity_values_destroy(&e->values);
    free(e->classname); free(e->think); free(e->reached); free(e->blocked); free(e->touch); free(e->use); free(e->pain); free(e->die); }
  free(s->entities);
  for (size_t i = 0; i < s->client_count; i++) { qa_q3_game_save_client_state *c = &s->clients[i]; qa_q3_persistant_values_destroy(&c->persistant);
    free(c->events); free(c->event_parms); free(c->persistant_slots); free(c->powerups); free(c->ammo_times); free(c->areabits); }
  free(s->clients); memset(s, 0, sizeof *s);
}
/* TS Array.from(slots.copy()). */
static double *copy_slots(const qa_q3_base_player_state_slots *slots, size_t *out) {
  int32_t *words = qa_q3_base_player_state_slots_copy(slots);
  if (!words) qa_range_error("out of memory");
  *out = (size_t)slots->length; double *d = malloc((*out ? *out : 1) * sizeof *d);
  if (!d) qa_range_error("out of memory");
  for (size_t i = 0; i < *out; i++) d[i] = words[i];
  free(words);
  return d;
}
static void restore_slots(qa_q3_base_player_state_slots *target, const double *values, size_t count) {
  if ((double)count != target->length) qa_range_error("Q3 saved player slot count mismatch");
  for (size_t i = 0; i < count; i++) qa_q3_base_player_state_slots_set(target, (double)i, values[i]);
}
static void entity_slot(qa_q3_entity_pool *pool, qa_q3_game_entity *e, bool *has, double *out) {
  *has = e != NULL; if (!e) return;
  if (qa_q3_entity_pool_get(pool, e->slot) != e) qa_range_error("Q3 graph contains a foreign entity record");
  *out = e->slot;
}
static double client_slot(qa_q3_entity_pool *pool, const qa_q3_game_client *c) {
  for (int i = 0; i < QA_Q3_MAX_CLIENTS; i++) if (pool->clients[i] == c) return i;
  qa_range_error("Q3 graph contains a foreign client record");
}
static qa_q3_saved_actor_ref saved_ref(qa_actor_id actor) { qa_saved_actor_id id = qa_saved_actor_id_of(actor); qa_q3_saved_actor_ref r = {true, id.slot, id.generation}; return r; }
static qa_saved_actor_id saved_id(qa_q3_saved_actor_ref r) { qa_saved_actor_id id = {r.slot, r.generation}; return id; }
qa_q3_graph_state qa_q3_capture_q3_graph(qa_q3_graph_services g) {
  qa_q3_entity_pool *pool = g.pool; qa_q3_callback_catalog *cb = &pool->callbacks;
  qa_q3_graph_state s; memset(&s, 0, sizeof s);
  s.complete = true; s.num_entities = pool->num_entities; s.max_clients = pool->max_clients;
  s.entity_count = QA_Q3_MAX_GENTITIES; s.entities = calloc(s.entity_count, sizeof *s.entities);
  s.ownership_count = QA_Q3_MAX_GENTITIES; s.ownership = calloc(s.ownership_count, sizeof *s.ownership);
  s.client_count = QA_Q3_MAX_CLIENTS; s.clients = calloc(s.client_count, sizeof *s.clients);
  if (!s.entities || !s.ownership || !s.clients) qa_range_error("out of memory");
  qa_q3_base_ownership_state *owners = qa_q3_base_entity_records_capture_ownership(g.records);
  if (!owners) qa_range_error("out of memory");
  for (size_t i = 0; i < s.ownership_count; i++) {
    s.ownership[i].has_actor = owners[i].has_actor; s.ownership[i].active = owners[i].active; s.ownership[i].borrowed = owners[i].borrowed;
    if (owners[i].has_actor) s.ownership[i].actor = saved_ref(owners[i].actor.id);
  }
  free(owners);
  for (size_t i = 0; i < s.entity_count; i++) {
    qa_q3_game_entity *e = qa_q3_entity_pool_at(pool, (double)i); qa_q3_game_save_entity_state *st = &s.entities[i];
    qa_q3_game_classname_capture classname = qa_q3_game_entity_capture_classname(e);
    if (classname.kind == 1) { st->classname_is_client = true; st->classname_client = client_slot(pool, classname.client); }
    else st->classname = dupstr(classname.value);
    if (e->has_activation) {
      if (e->activation.entity) { bool has; entity_slot(pool, e->activation.entity, &has, &st->activation_slot); st->activation_kind = 1; }
      else { st->activation_kind = 2; st->activation_actor = saved_ref(e->activation.actor); }
    }
    if (e->item) {
      st->has_item = true; st->item = -1;
      size_t item_count = 0; const qa_q3_base_item_definition *items = qa_q3_base_item_list(qa_q3_base_entity_records_product(g.records), &item_count);
      for (size_t k = 0; k < item_count; k++) if (&items[k] == e->item) { st->item = (double)k; break; }
      if (st->item < 0) qa_range_error("Q3 entity has an unknown item identity");
    }
    st->values = qa_q3_capture_entity_values(e); st->network = qa_q3_capture_network_values(&e->s);
    st->pos = e->s.pos; st->apos = e->s.apos; st->sv_flags = e->r.sv_flags; st->single_client = e->r.single_client;
    st->contents = e->r.contents; st->owner_num = e->r.owner_num; st->model_kind = e->r.model.kind; st->model_index = e->r.model.index;
    st->has_client = e->client != NULL; if (e->client) st->client = client_slot(pool, e->client);
    entity_slot(pool, e->parent, &st->has_parent, &st->parent); entity_slot(pool, e->next_train, &st->has_next_train, &st->next_train);
    entity_slot(pool, e->prev_train, &st->has_prev_train, &st->prev_train); entity_slot(pool, e->target_ent, &st->has_target_ent, &st->target_ent);
    entity_slot(pool, e->chain, &st->has_chain, &st->chain); entity_slot(pool, e->enemy, &st->has_enemy, &st->enemy);
    entity_slot(pool, e->activator, &st->has_activator, &st->activator); entity_slot(pool, e->teamchain, &st->has_teamchain, &st->teamchain);
    entity_slot(pool, e->teammaster, &st->has_teammaster, &st->teammaster);
    st->nextthink = e->nextthink;
    st->think = dupstr(qa_q3_callback_family_capture(&cb->think, e->think)); st->reached = dupstr(qa_q3_callback_family_capture(&cb->reached, e->reached));
    st->blocked = dupstr(qa_q3_callback_family_capture(&cb->blocked, e->blocked)); st->touch = dupstr(qa_q3_callback_family_capture(&cb->touch, e->touch));
    st->use = dupstr(qa_q3_callback_family_capture(&cb->use, e->use)); st->pain = dupstr(qa_q3_callback_family_capture(&cb->pain, e->pain));
    st->die = dupstr(qa_q3_callback_family_capture(&cb->die, e->die));
    qa_q3_base_entity_shared_private_state priv = qa_q3_base_entity_shared_capture_private_state(&e->r);
    st->has_abs_min_override = priv.has_abs_min_override; st->abs_min_override = priv.abs_min_override;
    st->has_abs_max_override = priv.has_abs_max_override; st->abs_max_override = priv.abs_max_override;
    if (priv.has_previous_link) {
      const qa_linked_body *link = &priv.previous_link;
      st->has_previous_link = true; st->previous_link_actor = saved_ref(link->actor); st->previous_link_count = link->link_count;
      st->previous_link_absolute_bounds = link->absolute_bounds; st->previous_link_origin = link->state.origin;
      st->previous_link_angles = link->state.angles; st->previous_link_velocity = link->state.velocity;
      st->previous_link_bounds = link->state.bounds;
      if (link->state.ground != NULL) st->previous_link_ground = saved_ref(*link->state.ground);
    }
  }
  for (size_t i = 0; i < s.client_count; i++) {
    qa_q3_game_client *c = pool->clients[i]; qa_q3_game_save_client_state *st = &s.clients[i];
    st->values = qa_q3_capture_client_values(c); st->player = qa_q3_capture_player_values(&c->ps);
    st->persistant = qa_q3_capture_persistant_values(&c->pers); st->command = c->pers.cmd;
    st->team = qa_q3_capture_team_values(&c->pers.team_state); st->session = qa_q3_capture_session_values(&c->sess);
    st->events = copy_slots(&c->ps.events, &st->event_count); st->event_parms = copy_slots(&c->ps.event_parms, &st->event_parm_count);
    st->persistant_slots = copy_slots(&c->ps.persistant, &st->persistant_count);
    st->powerups = copy_slots(&c->ps.powerups, &st->powerup_count); st->ammo_times = copy_slots(&c->ammo_times, &st->ammo_time_count);
    qa_q3_base_entity_records_capture_client_backing(g.records, (double)i, st->source_stats, st->special_ammo);
    entity_slot(pool, c->hook, &st->has_hook, &st->hook); entity_slot(pool, c->persistant_powerup, &st->has_persistant_powerup, &st->persistant_powerup);
    if (c->areabits) {
      st->has_areabits = true; st->areabits_length = c->areabits_length; st->areabits = malloc(c->areabits_length ? c->areabits_length : 1);
      if (!st->areabits) qa_range_error("out of memory");
      memcpy(st->areabits, c->areabits, c->areabits_length);
    }
  }
  return s;
}
void qa_q3_prepare_q3_graph(qa_q3_graph_services g, const qa_q3_graph_state *state) {
  if (state->entity_count != QA_Q3_MAX_GENTITIES || state->client_count != QA_Q3_MAX_CLIENTS) qa_range_error("Q3 save must retain all entity and client slots");
  qa_q3_base_ownership_state *owners = calloc(state->ownership_count ? state->ownership_count : 1, sizeof *owners);
  if (!owners) qa_range_error("out of memory");
  for (size_t i = 0; i < state->ownership_count; i++) {
    const qa_q3_graph_ownership *entry = &state->ownership[i];
    owners[i].has_actor = entry->has_actor; owners[i].active = entry->active; owners[i].borrowed = entry->borrowed;
    if (entry->has_actor && !qa_session_actor_registry_resolve_saved(g.actors, saved_id(entry->actor), &owners[i].actor))
      qa_range_error("Q3 saved ownership actor is not live");
  }
  qa_q3_base_entity_records_restore_ownership(g.records, owners, state->ownership_count);
  free(owners);
  for (size_t i = 0; i < state->client_count; i++)
    qa_q3_base_entity_records_restore_client_backing(g.records, (double)i, state->clients[i].source_stats, 16, state->clients[i].special_ammo, 16);
}
static qa_q3_game_entity *slot_entity(qa_q3_entity_pool *pool, bool has, double slot) { return has ? qa_q3_entity_pool_at(pool, slot) : NULL; }
static void restore_core(qa_q3_graph_services g, qa_q3_graph_state *state) {
  qa_q3_entity_pool *pool = g.pool; qa_q3_callback_catalog *cb = &pool->callbacks;
  for (size_t i = 0; i < state->entity_count; i++) {
    qa_q3_game_entity *e = qa_q3_entity_pool_at(pool, (double)i); qa_q3_game_save_entity_state *st = &state->entities[i];
    qa_q3_restore_entity_values(e, &st->values); qa_q3_restore_network_values(&e->s, st->network);
    e->s.pos = st->pos; e->s.apos = st->apos; e->r.sv_flags = st->sv_flags; e->r.single_client = st->single_client;
    e->r.model.kind = st->model_kind; e->r.model.index = st->model_index; e->r.contents = st->contents; e->r.owner_num = st->owner_num;
    e->client = st->has_client ? qa_q3_entity_pool_client_at(pool, st->client) : NULL;
    e->parent = slot_entity(pool, st->has_parent, st->parent); e->next_train = slot_entity(pool, st->has_next_train, st->next_train);
    e->prev_train = slot_entity(pool, st->has_prev_train, st->prev_train); e->target_ent = slot_entity(pool, st->has_target_ent, st->target_ent);
    e->chain = slot_entity(pool, st->has_chain, st->chain); e->enemy = slot_entity(pool, st->has_enemy, st->enemy);
    e->activator = slot_entity(pool, st->has_activator, st->activator); e->teamchain = slot_entity(pool, st->has_teamchain, st->teamchain);
    e->teammaster = slot_entity(pool, st->has_teammaster, st->teammaster);
    qa_q3_game_entity_restore_next_think(e, st->nextthink);
    e->think = qa_q3_callback_family_resolve(&cb->think, st->think); e->reached = qa_q3_callback_family_resolve(&cb->reached, st->reached);
    e->blocked = qa_q3_callback_family_resolve(&cb->blocked, st->blocked); e->touch = qa_q3_callback_family_resolve(&cb->touch, st->touch);
    e->use = qa_q3_callback_family_resolve(&cb->use, st->use); e->pain = qa_q3_callback_family_resolve(&cb->pain, st->pain);
    e->die = qa_q3_callback_family_resolve(&cb->die, st->die);
    if (state->complete) {
      if (st->classname_is_client) qa_q3_game_entity_bind_client_name(e, qa_q3_entity_pool_client_at(pool, st->classname_client));
      else qa_q3_game_entity_set_classname(e, st->classname);
      if (st->activation_kind == 1) { e->has_activation = true; e->activation = qa_q3_participant_entity(qa_q3_entity_pool_at(pool, st->activation_slot)); }
      else if (st->activation_kind == 2 && g.records) {
        e->has_activation = true;
        e->activation = qa_q3_base_entity_records_damage_inflictor(g.records, true, qa_session_actor_registry_reference_saved(g.actors, saved_id(st->activation_actor), "checkpoint"));
      } else e->has_activation = false;
    }
    if (st->has_item && g.records) {
      e->item = qa_q3_base_item_at(qa_q3_base_entity_records_product(g.records), st->item);
    } else if (!st->has_item) e->item = NULL;
    if (state->complete) {
      /* TS restorePrivateState; previous links need the actor registry. */
      qa_q3_base_entity_shared_private_state priv; memset(&priv, 0, sizeof priv); qa_actor_id ground;
      priv.has_abs_min_override = st->has_abs_min_override; priv.abs_min_override = st->abs_min_override;
      priv.has_abs_max_override = st->has_abs_max_override; priv.abs_max_override = st->abs_max_override;
      if (st->has_previous_link && g.actors) {
        qa_linked_body *link = &priv.previous_link; priv.has_previous_link = true;
        link->actor = qa_session_actor_registry_reference_saved(g.actors, saved_id(st->previous_link_actor), "checkpoint");
        link->link_count = st->previous_link_count; link->absolute_bounds = st->previous_link_absolute_bounds;
        link->state.origin = st->previous_link_origin; link->state.angles = st->previous_link_angles;
        link->state.velocity = st->previous_link_velocity; link->state.bounds = st->previous_link_bounds;
        if (st->previous_link_ground.present) {
          ground = qa_session_actor_registry_reference_saved(g.actors, saved_id(st->previous_link_ground), "checkpoint");
          link->state.ground = &ground;
        }
      }
      qa_q3_base_entity_shared_restore_private_state(&e->r, priv); /* copies ground into r */
    }
  }
  for (size_t i = 0; i < state->client_count; i++) {
    qa_q3_game_client *c = pool->clients[i]; qa_q3_game_save_client_state *st = &state->clients[i];
    qa_q3_restore_client_values(c, st->values); qa_q3_restore_player_values(&c->ps, st->player);
    qa_q3_restore_persistant_values(&c->pers, &st->persistant); c->pers.cmd = st->command;
    qa_q3_restore_team_values(&c->pers.team_state, st->team); qa_q3_restore_session_values(&c->sess, st->session);
    restore_slots(&c->ps.events, st->events, st->event_count); restore_slots(&c->ps.event_parms, st->event_parms, st->event_parm_count);
    restore_slots(&c->ps.persistant, st->persistant_slots, st->persistant_count); restore_slots(&c->ps.powerups, st->powerups, st->powerup_count);
    restore_slots(&c->ammo_times, st->ammo_times, st->ammo_time_count);
    c->hook = slot_entity(pool, st->has_hook, st->hook); c->persistant_powerup = slot_entity(pool, st->has_persistant_powerup, st->persistant_powerup);
    if (state->complete) {
      free(c->areabits); c->areabits = NULL; c->areabits_length = 0;
      if (st->has_areabits) {
        c->areabits = malloc(st->areabits_length ? st->areabits_length : 1); if (!c->areabits) qa_range_error("out of memory");
        memcpy(c->areabits, st->areabits, st->areabits_length); c->areabits_length = st->areabits_length;
      }
    }
  }
  qa_q3_pool_counts counts = {state->num_entities, state->max_clients}; qa_q3_entity_pool_restore_counts(pool, counts);
}
void qa_q3_restore_q3_graph(qa_q3_graph_services g, qa_q3_graph_state *state) {
  if (state->entity_count != QA_Q3_MAX_GENTITIES || state->client_count != QA_Q3_MAX_CLIENTS) qa_range_error("Q3 save must retain all entity and client slots");
  restore_core(g, state);
  qa_q3_base_entity_records_restore_callbacks(g.records);
}
void qa_q3_restore_graph(qa_q3_entity_pool *pool, qa_q3_graph_state *state) {
  if (state->entity_count != QA_Q3_MAX_GENTITIES || state->client_count != QA_Q3_MAX_CLIENTS) qa_range_error("Q3 save must retain all entity and client slots");
  qa_q3_graph_services g; memset(&g, 0, sizeof g); g.pool = pool;
  restore_core(g, state);
}
/* Plain tree writer. */
static qa_save_value *need(qa_save_value *v) { if (!v) qa_range_error("out of memory"); return v; }
static void put(qa_save_value *o, const char *k, qa_save_value *v) { if (!qa_save_value_set(o, k, need(v))) qa_range_error("out of memory"); }
static void put_num(qa_save_value *o, const char *k, double v) { put(o, k, qa_save_value_number(v)); }
static void put_bool(qa_save_value *o, const char *k, bool v) { put(o, k, qa_save_value_boolean(v)); }
static void put_str(qa_save_value *o, const char *k, const char *v) { put(o, k, v ? qa_save_value_string(v) : qa_save_value_null()); }
static qa_save_value *vec_value(qa_vec3 v) { qa_save_value *o = need(qa_save_value_object()); put_num(o, "x", v.x); put_num(o, "y", v.y); put_num(o, "z", v.z); return o; }
static void put_vec(qa_save_value *o, const char *k, qa_vec3 v) { put(o, k, vec_value(v)); }
static void put_slot(qa_save_value *o, const char *k, bool has, double v) { put(o, k, has ? qa_save_value_number(v) : qa_save_value_null()); }
static qa_save_value *actor_value(qa_q3_saved_actor_ref a) { qa_save_value *o = need(qa_save_value_object()); put_num(o, "slot", a.slot); put_num(o, "generation", a.generation); return o; }
static qa_save_value *numbers_value(const double *v, size_t n) {
  qa_save_value *a = need(qa_save_value_array());
  for (size_t i = 0; i < n; i++) if (!qa_save_value_push(a, need(qa_save_value_number(v[i])))) qa_range_error("out of memory");
  return a;
}
static qa_save_value *trajectory_value(qa_q3_base_trajectory t) {
  qa_save_value *o = need(qa_save_value_object()); put_num(o, "type", t.type); put_num(o, "time", t.time); put_num(o, "duration", t.duration); put_vec(o, "base", t.base); put_vec(o, "delta", t.delta); return o;
}
static qa_save_value *entity_values_value(const qa_q3_entity_values *v) {
  qa_save_value *o = need(qa_save_value_object());
  put_num(o, "spawnflags", v->spawnflags); put_bool(o, "neverFree", v->never_free); put_num(o, "flags", v->flags); put_str(o, "model", v->model); put_str(o, "model2", v->model2);
  put_num(o, "freetime", v->freetime); put_num(o, "eventTime", v->event_time); put_bool(o, "freeAfterEvent", v->free_after_event); put_bool(o, "unlinkAfterEvent", v->unlink_after_event);
  put_bool(o, "physicsObject", v->physics_object); put_num(o, "physicsBounce", v->physics_bounce); put_num(o, "clipmask", v->clipmask); put_num(o, "moverState", v->mover_state);
  put_num(o, "soundPos1", v->sound_pos1); put_num(o, "sound1to2", v->sound_1to2); put_num(o, "sound2to1", v->sound_2to1); put_num(o, "soundPos2", v->sound_pos2); put_num(o, "soundLoop", v->sound_loop);
  put_vec(o, "pos1", v->pos1); put_vec(o, "pos2", v->pos2); put_str(o, "message", v->message); put_num(o, "timestamp", v->timestamp); put_num(o, "angle", v->angle);
  put_str(o, "target", v->target); put_str(o, "targetname", v->targetname); put_str(o, "team", v->team); put_str(o, "targetShaderName", v->target_shader_name); put_str(o, "targetShaderNewName", v->target_shader_new_name);
  put_num(o, "speed", v->speed); put_vec(o, "movedir", v->movedir); put_num(o, "painDebounceTime", v->pain_debounce_time); put_num(o, "flySoundDebounceTime", v->fly_sound_debounce_time);
  put_num(o, "lastMoveTime", v->last_move_time); put_num(o, "damage", v->damage); put_num(o, "splashDamage", v->splash_damage); put_num(o, "splashRadius", v->splash_radius);
  put_num(o, "methodOfDeath", v->method_of_death); put_num(o, "splashMethodOfDeath", v->splash_method_of_death); put_num(o, "count", v->count);
  put_num(o, "kamikazeTime", v->kamikaze_time); put_num(o, "kamikazeShockTime", v->kamikaze_shock_time); put_num(o, "watertype", v->watertype); put_num(o, "waterlevel", v->waterlevel);
  put_num(o, "noiseIndex", v->noise_index); put_num(o, "wait", v->wait); put_num(o, "random", v->random); return o;
}
static qa_save_value *network_value(const qa_q3_network_values *v) {
  qa_save_value *o = need(qa_save_value_object());
  put_num(o, "number", v->number); put_num(o, "eType", v->e_type); put_num(o, "eFlags", v->e_flags); put_num(o, "time", v->time); put_num(o, "time2", v->time2);
  put_vec(o, "origin", v->origin); put_vec(o, "origin2", v->origin2); put_vec(o, "angles", v->angles); put_vec(o, "angles2", v->angles2);
  put_num(o, "otherEntityNum", v->other_entity_num); put_num(o, "otherEntityNum2", v->other_entity_num2); put_num(o, "groundEntityNum", v->ground_entity_num);
  put_num(o, "constantLight", v->constant_light); put_num(o, "loopSound", v->loop_sound); put_num(o, "modelindex", v->modelindex); put_num(o, "modelindex2", v->modelindex2);
  put_num(o, "clientNum", v->client_num); put_num(o, "frame", v->frame); put_num(o, "solid", v->solid); put_num(o, "event", v->event); put_num(o, "eventParm", v->event_parm);
  put_num(o, "powerups", v->powerups); put_num(o, "weapon", v->weapon); put_num(o, "legsAnim", v->legs_anim); put_num(o, "torsoAnim", v->torso_anim); put_num(o, "generic1", v->generic1); return o;
}
static qa_save_value *entity_value(const qa_q3_game_save_entity_state *s) {
  qa_save_value *o = need(qa_save_value_object()), *shared = need(qa_save_value_object()), *model = need(qa_save_value_object()), *priv = need(qa_save_value_object()), *name = need(qa_save_value_object());
  put(o, "values", entity_values_value(&s->values)); put(o, "network", network_value(&s->network)); put(o, "pos", trajectory_value(s->pos)); put(o, "apos", trajectory_value(s->apos));
  put_str(model, "kind", s->model_kind == QA_Q3_BASE_MODEL_INLINE ? "inline" : s->model_kind == QA_Q3_BASE_MODEL_CAPSULE ? "capsule" : "box");
  if (s->model_kind == QA_Q3_BASE_MODEL_INLINE) put_num(model, "index", s->model_index);
  put_num(shared, "svFlags", s->sv_flags); put_num(shared, "singleClient", s->single_client); put(shared, "model", model); put_num(shared, "contents", s->contents); put_num(shared, "ownerNum", s->owner_num);
  put(o, "shared", shared);
  /* save-state.ts literal order: absMinOverride, absMaxOverride, previousLink. */
  put(priv, "absMinOverride", s->has_abs_min_override ? vec_value(s->abs_min_override) : qa_save_value_null());
  put(priv, "absMaxOverride", s->has_abs_max_override ? vec_value(s->abs_max_override) : qa_save_value_null());
  if (!s->has_previous_link) put(priv, "previousLink", qa_save_value_null());
  else {
    qa_save_value *link = need(qa_save_value_object()), *ab = need(qa_save_value_object()), *st = need(qa_save_value_object()), *bb = need(qa_save_value_object());
    put(link, "actor", actor_value(s->previous_link_actor)); put_num(link, "linkCount", s->previous_link_count);
    put_vec(ab, "min", s->previous_link_absolute_bounds.min); put_vec(ab, "max", s->previous_link_absolute_bounds.max); put(link, "absoluteBounds", ab);
    put_vec(st, "origin", s->previous_link_origin); put_vec(st, "angles", s->previous_link_angles); put_vec(st, "velocity", s->previous_link_velocity);
    put_vec(bb, "min", s->previous_link_bounds.min); put_vec(bb, "max", s->previous_link_bounds.max); put(st, "bounds", bb);
    put(st, "ground", s->previous_link_ground.present ? actor_value(s->previous_link_ground) : qa_save_value_null());
    put(link, "state", st); put(priv, "previousLink", link);
  }
  put(o, "sharedPrivate", priv);
  put_slot(o, "client", s->has_client, s->client);
  if (s->classname_is_client) { put_str(name, "kind", "client-name"); put_num(name, "client", s->classname_client); }
  else { put_str(name, "kind", "value"); put_str(name, "value", s->classname); }
  put(o, "classname", name);
  put_slot(o, "parent", s->has_parent, s->parent); put_slot(o, "nextTrain", s->has_next_train, s->next_train); put_slot(o, "prevTrain", s->has_prev_train, s->prev_train);
  put_slot(o, "targetEnt", s->has_target_ent, s->target_ent); put_slot(o, "chain", s->has_chain, s->chain); put_slot(o, "enemy", s->has_enemy, s->enemy);
  put_slot(o, "activator", s->has_activator, s->activator); put_slot(o, "teamchain", s->has_teamchain, s->teamchain); put_slot(o, "teammaster", s->has_teammaster, s->teammaster);
  if (s->activation_kind == 0) put(o, "activation", qa_save_value_null());
  else {
    qa_save_value *a = need(qa_save_value_object());
    if (s->activation_kind == 1) { put_str(a, "kind", "entity"); put_num(a, "slot", s->activation_slot); }
    else { put_str(a, "kind", "actor"); put(a, "actor", actor_value(s->activation_actor)); }
    put(o, "activation", a);
  }
  put_slot(o, "item", s->has_item, s->item); put_num(o, "nextthink", s->nextthink);
  put_str(o, "think", s->think); put_str(o, "reached", s->reached); put_str(o, "blocked", s->blocked); put_str(o, "touch", s->touch); put_str(o, "use", s->use); put_str(o, "pain", s->pain); put_str(o, "die", s->die);
  return o;
}
static qa_save_value *client_value(const qa_q3_game_save_client_state *s) {
  qa_save_value *o = need(qa_save_value_object()), *v = need(qa_save_value_object()), *p = need(qa_save_value_object()), *ps = need(qa_save_value_object()), *c = need(qa_save_value_object()), *t = need(qa_save_value_object()), *se = need(qa_save_value_object()), *b = need(qa_save_value_object());
  const qa_q3_client_values *cv = &s->values;
  put_bool(v, "readyToExit", cv->ready_to_exit); put_bool(v, "noclip", cv->noclip); put_num(v, "lastCmdTime", cv->last_cmd_time); put_num(v, "buttons", cv->buttons); put_num(v, "oldButtons", cv->old_buttons);
  put_num(v, "latchedButtons", cv->latched_buttons); put_vec(v, "oldOrigin", cv->old_origin); put_num(v, "damageArmor", cv->damage_armor); put_num(v, "damageBlood", cv->damage_blood); put_num(v, "damageKnockback", cv->damage_knockback);
  put_vec(v, "damageFrom", cv->damage_from); put_bool(v, "damageFromWorld", cv->damage_from_world); put_num(v, "accurateCount", cv->accurate_count); put_num(v, "accuracyShots", cv->accuracy_shots); put_num(v, "accuracyHits", cv->accuracy_hits);
  put_num(v, "lastKilledClient", cv->last_killed_client); put_num(v, "lastHurtClient", cv->last_hurt_client); put_num(v, "lastHurtMod", cv->last_hurt_mod); put_num(v, "respawnTime", cv->respawn_time); put_num(v, "inactivityTime", cv->inactivity_time);
  put_bool(v, "inactivityWarning", cv->inactivity_warning); put_num(v, "rewardTime", cv->reward_time); put_num(v, "airOutTime", cv->air_out_time); put_num(v, "lastKillTime", cv->last_kill_time); put_bool(v, "fireHeld", cv->fire_held);
  put_num(v, "switchTeamTime", cv->switch_team_time); put_num(v, "timeResidual", cv->time_residual); put_num(v, "portalID", cv->portal_id); put_num(v, "invulnerabilityTime", cv->invulnerability_time);
  put(o, "values", v);
  const qa_q3_player_values *pv = &s->player;
  put_num(p, "commandTime", pv->command_time); put_num(p, "pmType", pv->pm_type); put_num(p, "bobCycle", pv->bob_cycle); put_num(p, "pmFlags", pv->pm_flags); put_num(p, "pmTime", pv->pm_time); put_num(p, "weaponTime", pv->weapon_time);
  put_num(p, "gravity", pv->gravity); put_num(p, "speed", pv->speed); put_vec(p, "deltaAngles", pv->delta_angles); put_num(p, "groundEntityNum", pv->ground_entity_num); put_num(p, "legsTimer", pv->legs_timer); put_num(p, "legsAnim", pv->legs_anim); put_num(p, "torsoTimer", pv->torso_timer);
  put_num(p, "torsoAnim", pv->torso_anim); put_num(p, "movementDir", pv->movement_dir); put_vec(p, "grapplePoint", pv->grapple_point); put_num(p, "eFlags", pv->e_flags); put_num(p, "eventSequence", pv->event_sequence);
  put_num(p, "externalEvent", pv->external_event); put_num(p, "externalEventParm", pv->external_event_parm); put_num(p, "externalEventTime", pv->external_event_time); put_num(p, "clientNum", pv->client_num); put_num(p, "weapon", pv->weapon);
  put_num(p, "weaponState", pv->weapon_state); put_vec(p, "viewangles", pv->viewangles); put_num(p, "viewheight", pv->viewheight); put_num(p, "damageEvent", pv->damage_event); put_num(p, "damageYaw", pv->damage_yaw); put_num(p, "damagePitch", pv->damage_pitch);
  put_num(p, "damageCount", pv->damage_count); put_num(p, "generic1", pv->generic1); put_num(p, "loopSound", pv->loop_sound); put_num(p, "jumppadEnt", pv->jumppad_ent); put_num(p, "ping", pv->ping); put_num(p, "pmoveFramecount", pv->pmove_framecount);
  put_num(p, "jumppadFrame", pv->jumppad_frame); put_num(p, "entityEventSequence", pv->entity_event_sequence);
  put(o, "player", p);
  const qa_q3_persistant_values *pe = &s->persistant;
  put_num(ps, "connected", pe->connected); put_bool(ps, "localClient", pe->local_client); put_bool(ps, "initialSpawn", pe->initial_spawn); put_bool(ps, "predictItemPickup", pe->predict_item_pickup); put_bool(ps, "pmoveFixed", pe->pmove_fixed);
  put_str(ps, "netname", pe->netname); put_num(ps, "maxHealth", pe->max_health); put_num(ps, "enterTime", pe->enter_time); put_num(ps, "voteCount", pe->vote_count); put_num(ps, "teamVoteCount", pe->team_vote_count); put_bool(ps, "teamInfo", pe->team_info);
  put(o, "persistant", ps);
  put_num(c, "serverTime", s->command.server_time); put_vec(c, "angles", s->command.angles); put_num(c, "buttons", s->command.buttons); put_num(c, "weapon", s->command.weapon);
  put_num(c, "forwardmove", s->command.forwardmove); put_num(c, "rightmove", s->command.rightmove); put_num(c, "upmove", s->command.upmove); put(o, "command", c);
  put_num(t, "state", s->team.state); put_num(t, "location", s->team.location); put_num(t, "captures", s->team.captures); put_num(t, "baseDefense", s->team.base_defense); put_num(t, "carrierDefense", s->team.carrier_defense);
  put_num(t, "flagRecovery", s->team.flag_recovery); put_num(t, "fragCarrier", s->team.frag_carrier); put_num(t, "assists", s->team.assists); put_num(t, "lastHurtCarrier", s->team.last_hurt_carrier);
  put_num(t, "lastReturnedFlag", s->team.last_returned_flag); put_num(t, "flagSince", s->team.flag_since); put_num(t, "lastFraggedCarrier", s->team.last_fragged_carrier); put(o, "team", t);
  put_num(se, "sessionTeam", s->session.session_team); put_num(se, "spectatorTime", s->session.spectator_time); put_num(se, "spectatorState", s->session.spectator_state); put_num(se, "spectatorClient", s->session.spectator_client);
  put_num(se, "wins", s->session.wins); put_num(se, "losses", s->session.losses); put_num(se, "teamLeader", s->session.team_leader); put(o, "session", se);
  put(o, "events", numbers_value(s->events, s->event_count)); put(o, "eventParms", numbers_value(s->event_parms, s->event_parm_count)); put(o, "persistantSlots", numbers_value(s->persistant_slots, s->persistant_count));
  put(o, "powerups", numbers_value(s->powerups, s->powerup_count)); put(o, "ammoTimes", numbers_value(s->ammo_times, s->ammo_time_count));
  put(b, "sourceStats", numbers_value(s->source_stats, 16)); put(b, "specialAmmo", numbers_value(s->special_ammo, 16)); put(o, "backing", b);
  put_slot(o, "hook", s->has_hook, s->hook); put_slot(o, "persistantPowerup", s->has_persistant_powerup, s->persistant_powerup);
  put(o, "areabits", s->has_areabits ? qa_save_value_bytes(s->areabits, s->areabits_length) : qa_save_value_null());
  return o;
}
qa_save_value *qa_q3_graph_state_to_value(const qa_q3_graph_state *s) {
  qa_save_value *o = need(qa_save_value_object()), *own = need(qa_save_value_array()), *ents = need(qa_save_value_array()), *cl = need(qa_save_value_array());
  for (size_t i = 0; i < s->ownership_count; i++) {
    qa_save_value *e = need(qa_save_value_object()); const qa_q3_graph_ownership *w = &s->ownership[i];
    put(e, "actor", w->has_actor ? actor_value(w->actor) : qa_save_value_null()); put_bool(e, "active", w->active); put_bool(e, "borrowed", w->borrowed);
    if (!qa_save_value_push(own, e)) qa_range_error("out of memory");
  }
  for (size_t i = 0; i < s->entity_count; i++) if (!qa_save_value_push(ents, entity_value(&s->entities[i]))) qa_range_error("out of memory");
  for (size_t i = 0; i < s->client_count; i++) if (!qa_save_value_push(cl, client_value(&s->clients[i]))) qa_range_error("out of memory");
  put(o, "ownership", own); put_num(o, "numEntities", s->num_entities); put_num(o, "maxClients", s->max_clients); put(o, "entities", ents); put(o, "clients", cl);
  return o;
}
/* Fields qa_q3_read_graph omits. */
static qa_save_reader field_of(const qa_save_reader *r, const char *n) { qa_save_reader f; if (!qa_save_reader_field(r, n, &f)) qa_range_error("out of memory"); return f; }
static qa_vec3 vec_field(const qa_save_reader *r, const char *n) { qa_save_reader f = field_of(r, n); qa_vec3 v = qa_q3_read_vector(&f); qa_save_reader_destroy(&f); return v; }
static double slot_of(const qa_save_reader *r, double maximum) { double v = qa_save_reader_integer_min(r, 0); if (v >= maximum) qa_save_reader_fail(r, "Q3 source reference outside retained table"); return v; }
static qa_q3_saved_actor_ref nullable_actor(const qa_save_reader *r, const char *n) {
  qa_q3_saved_actor_ref out = {false, 0, 0}; qa_save_reader f = field_of(r, n);
  if (!qa_save_reader_is_null(&f)) out = qa_q3_read_actor(&f);
  qa_save_reader_destroy(&f); return out;
}
static double *numbers_field(const qa_save_reader *r, const char *n, size_t *count) {
  qa_save_reader f = field_of(r, n); *count = qa_save_reader_list_count(&f);
  double *d = malloc((*count ? *count : 1) * sizeof *d); if (!d) qa_range_error("out of memory");
  for (size_t i = 0; i < *count; i++) { qa_save_reader it; qa_save_reader_item(&f, i, &it); d[i] = qa_save_reader_number(&it); qa_save_reader_destroy(&it); }
  qa_save_reader_destroy(&f); return d;
}
static void read_entity_extras(const qa_save_reader *r, qa_q3_game_save_entity_state *s) {
  static const char *const model_kinds[] = {"inline", "box", "capsule"}, *const name_kinds[] = {"value", "client-name"}, *const activation_kinds[] = {"entity", "actor"};
  qa_save_reader shared = field_of(r, "shared"), model = field_of(&shared, "model"), kind = field_of(&model, "kind");
  const char *k = qa_save_reader_choice_string(&kind, model_kinds, 3);
  s->model_kind = strcmp(k, "inline") == 0 ? QA_Q3_BASE_MODEL_INLINE : strcmp(k, "capsule") == 0 ? QA_Q3_BASE_MODEL_CAPSULE : QA_Q3_BASE_MODEL_BOX;
  if (s->model_kind == QA_Q3_BASE_MODEL_INLINE) { qa_save_reader i = field_of(&model, "index"); s->model_index = qa_save_reader_integer_min(&i, 0); qa_save_reader_destroy(&i); }
  qa_save_reader_destroy(&kind); qa_save_reader_destroy(&model); qa_save_reader_destroy(&shared);
  qa_save_reader name = field_of(r, "classname"), nk = field_of(&name, "kind");
  if (strcmp(qa_save_reader_choice_string(&nk, name_kinds, 2), "value") == 0) {
    qa_save_reader v = field_of(&name, "value"); s->classname = qa_save_reader_is_null(&v) ? NULL : dupstr(qa_save_reader_string(&v)); qa_save_reader_destroy(&v);
  } else { qa_save_reader c = field_of(&name, "client"); s->classname_is_client = true; s->classname_client = slot_of(&c, QA_Q3_MAX_CLIENTS); qa_save_reader_destroy(&c); }
  qa_save_reader_destroy(&nk); qa_save_reader_destroy(&name);
  qa_save_reader activation = field_of(r, "activation");
  if (!qa_save_reader_is_null(&activation)) {
    qa_save_reader ak = field_of(&activation, "kind");
    if (strcmp(qa_save_reader_choice_string(&ak, activation_kinds, 2), "entity") == 0) {
      qa_save_reader sl = field_of(&activation, "slot"); s->activation_kind = 1; s->activation_slot = slot_of(&sl, QA_Q3_MAX_GENTITIES); qa_save_reader_destroy(&sl);
    } else { qa_save_reader a = field_of(&activation, "actor"); s->activation_kind = 2; s->activation_actor = qa_q3_read_actor(&a); qa_save_reader_destroy(&a); }
    qa_save_reader_destroy(&ak);
  }
  qa_save_reader_destroy(&activation);
  /* TS sharedPrivate, read in the TS field order. */
  qa_save_reader priv = field_of(r, "sharedPrivate"), f = field_of(&priv, "absMinOverride");
  if (!qa_save_reader_is_null(&f)) { s->has_abs_min_override = true; s->abs_min_override = qa_q3_read_vector(&f); }
  qa_save_reader_destroy(&f); f = field_of(&priv, "absMaxOverride");
  if (!qa_save_reader_is_null(&f)) { s->has_abs_max_override = true; s->abs_max_override = qa_q3_read_vector(&f); }
  qa_save_reader_destroy(&f); f = field_of(&priv, "previousLink");
  if (!qa_save_reader_is_null(&f)) {
    qa_save_reader state = field_of(&f, "state"), a = field_of(&f, "actor"), count = field_of(&f, "linkCount"), ab = field_of(&f, "absoluteBounds"), bb = field_of(&state, "bounds");
    s->has_previous_link = true; s->previous_link_actor = qa_q3_read_actor(&a); s->previous_link_count = qa_save_reader_integer_min(&count, 0);
    s->previous_link_absolute_bounds.min = vec_field(&ab, "min"); s->previous_link_absolute_bounds.max = vec_field(&ab, "max");
    s->previous_link_origin = vec_field(&state, "origin"); s->previous_link_angles = vec_field(&state, "angles"); s->previous_link_velocity = vec_field(&state, "velocity");
    s->previous_link_bounds.min = vec_field(&bb, "min"); s->previous_link_bounds.max = vec_field(&bb, "max");
    s->previous_link_ground = nullable_actor(&state, "ground");
    qa_save_reader_destroy(&bb); qa_save_reader_destroy(&ab); qa_save_reader_destroy(&count); qa_save_reader_destroy(&a); qa_save_reader_destroy(&state);
  }
  qa_save_reader_destroy(&f); qa_save_reader_destroy(&priv);
}
static void read_client_extras(const qa_save_reader *r, qa_q3_game_save_client_state *s) {
  qa_save_reader b = field_of(r, "backing"); size_t n; double *values;
  values = numbers_field(&b, "sourceStats", &n); if (n != 16) qa_save_reader_fail(&b, "Invalid Q3 private client backing"); memcpy(s->source_stats, values, sizeof s->source_stats); free(values);
  values = numbers_field(&b, "specialAmmo", &n); if (n != 16) qa_save_reader_fail(&b, "Invalid Q3 private client backing"); memcpy(s->special_ammo, values, sizeof s->special_ammo); free(values);
  qa_save_reader_destroy(&b);
  qa_save_reader a = field_of(r, "areabits");
  if (!qa_save_reader_is_null(&a)) {
    size_t length = 0; const uint8_t *bytes = qa_save_reader_bytes(&a, &length);
    s->has_areabits = true; s->areabits_length = length; s->areabits = malloc(length ? length : 1); if (!s->areabits) qa_range_error("out of memory");
    memcpy(s->areabits, bytes, length);
  }
  qa_save_reader_destroy(&a);
}
qa_q3_graph_state qa_q3_read_complete_graph(const qa_save_value *value) {
  qa_q3_graph_state s = qa_q3_read_graph(value);
  qa_save_reader reader; qa_save_reader_init(&reader, value, "q3.graph");
  qa_save_reader list = field_of(&reader, "entities");
  for (size_t i = 0; i < s.entity_count; i++) { qa_save_reader it; qa_save_reader_item(&list, i, &it); read_entity_extras(&it, &s.entities[i]); qa_save_reader_destroy(&it); }
  qa_save_reader_destroy(&list);
  list = field_of(&reader, "clients");
  for (size_t i = 0; i < s.client_count; i++) { qa_save_reader it; qa_save_reader_item(&list, i, &it); read_client_extras(&it, &s.clients[i]); qa_save_reader_destroy(&it); }
  qa_save_reader_destroy(&list); qa_save_reader_destroy(&reader);
  s.complete = true; return s;
}
