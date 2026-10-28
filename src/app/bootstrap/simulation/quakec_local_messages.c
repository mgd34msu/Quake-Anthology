/* Port of src/app/bootstrap/simulation/quakec-local-messages.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/quakec_local_messages.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
enum { ST_SERVER_VARS, ST_SET_VIEWS, ST_SEQUENCE, ST_LEVEL_COMPLETED, ST_BACK_TO_LOBBY,
  ST_PROMPT_BEGIN, ST_PROMPT_CHOICE, ST_STAT, ST_LIGHT_STYLE, ST_NAME, ST_COLORS, ST_FRAGS,
  ST_PING, ST_SOCIAL, ST_PLAYER_INFO, ST_SET_VIEW, ST_SET_ANGLE, ST_PAUSE, ST_CD_TRACK,
  ST_FOG, ST_SKYBOX, ST_INTERMISSION };
static int state_key(int category, int discriminator) { return discriminator * 32 + category; }
#define MAX_ENTRIES 512
#define MAX_CLIENTS 64
typedef struct local_state { int keys[MAX_ENTRIES]; qa_netquake_message values[MAX_ENTRIES]; size_t count; } local_state;
struct qa_app_sim_quakec_local_messages {
  local_state baseline;
  qa_actor_id client_actors[MAX_CLIENTS]; local_state clients[MAX_CLIENTS]; size_t client_count;
  bool has_baseline_view; qa_actor_id baseline_view;
  qa_actor_id view_actors[MAX_CLIENTS], view_targets[MAX_CLIENTS]; size_t view_count;
  qa_netquake_message scratch[MAX_ENTRIES]; size_t scratch_count;
};
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static int find_key(local_state *s, int key) {
  for (size_t i = 0; i < s->count; i++) if (s->keys[i] == key) return (int)i;
  return -1;
}
static void set_key(local_state *s, int key, qa_netquake_message m) {
  int at = find_key(s, key);
  if (at >= 0) { s->values[at] = m; return; }
  if (s->count >= MAX_ENTRIES) qa_range_error("Out of memory");
  s->keys[s->count] = key; s->values[s->count++] = m;
}
static qa_netquake_message *get_key(local_state *s, int key) {
  int at = find_key(s, key); return at >= 0 ? &s->values[at] : NULL;
}
static void delete_key(local_state *s, int key) {
  int at = find_key(s, key); if (at < 0) return;
  memmove(&s->keys[at], &s->keys[at + 1], (s->count - (size_t)at - 1) * sizeof s->keys[0]);
  memmove(&s->values[at], &s->values[at + 1], (s->count - (size_t)at - 1) * sizeof s->values[0]);
  s->count--;
}
static void retained(local_state *state, qa_netquake_message m) {
  const char *k = m.kind;
  if (is(k, "server-vars")) set_key(state, state_key(ST_SERVER_VARS, 0), m);
  else if (is(k, "set-views")) set_key(state, state_key(ST_SET_VIEWS, 0), m);
  else if (is(k, "sequence")) set_key(state, state_key(ST_SEQUENCE, 0), m);
  else if (is(k, "level-completed")) set_key(state, state_key(ST_LEVEL_COMPLETED, 0), m);
  else if (is(k, "back-to-lobby")) set_key(state, state_key(ST_BACK_TO_LOBBY, 0), m);
  else if (is(k, "prompt-begin") || is(k, "prompt-clear")) {
    for (size_t i = state->count; i-- > 0;)
      if (state->keys[i] % 32 == ST_PROMPT_BEGIN || state->keys[i] % 32 == ST_PROMPT_CHOICE) delete_key(state, state->keys[i]);
    set_key(state, state_key(ST_PROMPT_BEGIN, 0), m);
  } else if (is(k, "prompt-choice")) {
    qa_netquake_message *begin = get_key(state, state_key(ST_PROMPT_BEGIN, 0));
    if (begin && is(begin->kind, "prompt-begin") && begin->text && begin->text[0] != '\0') {
      int count = 0;
      for (size_t i = 0; i < state->count; i++) if (is(state->values[i].kind, "prompt-choice")) count++;
      set_key(state, state_key(ST_PROMPT_CHOICE, count), m);
    }
  } else if (is(k, "stat")) {
    if (m.index != trunc(m.index) || m.index < 0 || m.index >= 32) qa_range_error("Invalid NetQuake client stat");
    set_key(state, state_key(ST_STAT, (int)m.index), m);
  } else if (is(k, "killed-monster") || is(k, "found-secret")) {
    int index = is(k, "killed-monster") ? 14 : 13;
    qa_netquake_message *old = get_key(state, state_key(ST_STAT, index));
    qa_netquake_message stat; memset(&stat, 0, sizeof stat);
    stat.kind = "stat"; stat.index = index; stat.value = (old && is(old->kind, "stat") ? old->value : 0) + 1;
    set_key(state, state_key(ST_STAT, index), stat);
  } else if (is(k, "spawned-monster")) {
    qa_netquake_message *old = get_key(state, state_key(ST_STAT, 12));
    qa_netquake_message stat; memset(&stat, 0, sizeof stat);
    stat.kind = "stat"; stat.index = 12; stat.value = (old && is(old->kind, "stat") ? old->value : 0) + m.value;
    set_key(state, state_key(ST_STAT, 12), stat);
  } else if (is(k, "light-style")) set_key(state, state_key(ST_LIGHT_STYLE, (int)m.index), m);
  else if (is(k, "name")) set_key(state, state_key(ST_NAME, (int)m.slot), m);
  else if (is(k, "colors")) set_key(state, state_key(ST_COLORS, (int)m.slot), m);
  else if (is(k, "frags")) set_key(state, state_key(ST_FRAGS, (int)m.slot), m);
  else if (is(k, "ping")) set_key(state, state_key(ST_PING, (int)m.slot), m);
  else if (is(k, "social")) set_key(state, state_key(ST_SOCIAL, (int)m.slot), m);
  else if (is(k, "player-info")) set_key(state, state_key(ST_PLAYER_INFO, (int)m.slot), m);
  else if (is(k, "set-view")) set_key(state, state_key(ST_SET_VIEW, 0), m);
  else if (is(k, "set-angle")) set_key(state, state_key(ST_SET_ANGLE, 0), m);
  else if (is(k, "pause")) set_key(state, state_key(ST_PAUSE, 0), m);
  else if (is(k, "cd-track")) set_key(state, state_key(ST_CD_TRACK, 0), m);
  else if (is(k, "fog")) set_key(state, state_key(ST_FOG, 0), m);
  else if (is(k, "skybox")) set_key(state, state_key(ST_SKYBOX, 0), m);
  else if (is(k, "intermission") || is(k, "finale") || is(k, "cutscene")) set_key(state, state_key(ST_INTERMISSION, 0), m);
}
static local_state *client_of(qa_app_sim_quakec_local_messages *s, qa_actor_id actor) {
  for (size_t i = 0; i < s->client_count; i++) if (qa_same_actor(s->client_actors[i], actor)) return &s->clients[i];
  return NULL;
}
static void view_set(qa_app_sim_quakec_local_messages *s, qa_actor_id actor, qa_actor_id target) {
  for (size_t i = 0; i < s->view_count; i++) if (qa_same_actor(s->view_actors[i], actor)) { s->view_targets[i] = target; return; }
  if (s->view_count >= MAX_CLIENTS) qa_range_error("Out of memory");
  s->view_actors[s->view_count] = actor; s->view_targets[s->view_count++] = target;
}
static void view_delete(qa_app_sim_quakec_local_messages *s, qa_actor_id actor) {
  for (size_t i = 0; i < s->view_count; i++) if (qa_same_actor(s->view_actors[i], actor)) {
    memmove(&s->view_actors[i], &s->view_actors[i + 1], (s->view_count - i - 1) * sizeof s->view_actors[0]);
    memmove(&s->view_targets[i], &s->view_targets[i + 1], (s->view_count - i - 1) * sizeof s->view_targets[0]);
    s->view_count--; return;
  }
}
qa_app_sim_quakec_local_messages *qa_app_sim_quakec_local_messages_create(void) {
  qa_app_sim_quakec_local_messages *s = calloc(1, sizeof *s);
  if (!s) qa_range_error("Out of memory");
  return s;
}
void qa_app_sim_quakec_local_messages_destroy(qa_app_sim_quakec_local_messages *state) { free(state); }
void qa_app_sim_quakec_local_messages_admit(qa_app_sim_quakec_local_messages *state, qa_actor_id actor) {
  if (client_of(state, actor)) return;
  if (state->client_count >= MAX_CLIENTS) qa_range_error("Out of memory");
  state->client_actors[state->client_count] = actor;
  state->clients[state->client_count] = state->baseline;
  state->client_count++;
  if (state->has_baseline_view) view_set(state, actor, state->baseline_view);
}
bool qa_app_sim_quakec_local_messages_has_client(qa_app_sim_quakec_local_messages *state, qa_actor_id actor) {
  return client_of(state, actor) != NULL;
}
void qa_app_sim_quakec_local_messages_retire(qa_app_sim_quakec_local_messages *state, qa_actor_id actor) {
  for (size_t i = 0; i < state->client_count; i++) if (qa_same_actor(state->client_actors[i], actor)) {
    memmove(&state->client_actors[i], &state->client_actors[i + 1], (state->client_count - i - 1) * sizeof state->client_actors[0]);
    memmove(&state->clients[i], &state->clients[i + 1], (state->client_count - i - 1) * sizeof state->clients[0]);
    state->client_count--; break;
  }
  view_delete(state, actor);
  if (state->has_baseline_view && qa_same_actor(state->baseline_view, actor)) state->has_baseline_view = false;
  for (size_t i = state->view_count; i-- > 0;)
    if (qa_same_actor(state->view_targets[i], actor)) view_delete(state, state->view_actors[i]);
}
void qa_app_sim_quakec_local_messages_receive(qa_app_sim_quakec_local_messages *state,
    const qa_netquake_message *messages, size_t message_count,
    const qa_actor_id *actor, bool has_view_target, const qa_actor_id *view_target) {
  if (actor) qa_app_sim_quakec_local_messages_admit(state, *actor);
  for (size_t i = 0; i < message_count; i++) if (is(messages[i].kind, "set-view") && has_view_target) {
    if (actor == NULL) {
      state->has_baseline_view = view_target != NULL;
      if (view_target) state->baseline_view = *view_target;
      for (size_t c = 0; c < state->client_count; c++) {
        if (view_target == NULL) view_delete(state, state->client_actors[c]);
        else view_set(state, state->client_actors[c], *view_target);
      }
    } else if (view_target == NULL) view_delete(state, *actor);
    else view_set(state, *actor, *view_target);
  }
  if (actor == NULL) {
    for (size_t i = 0; i < message_count; i++) {
      retained(&state->baseline, messages[i]);
      for (size_t c = 0; c < state->client_count; c++) retained(&state->clients[c], messages[i]);
    }
  } else {
    qa_app_sim_quakec_local_messages_admit(state, *actor);
    local_state *client = client_of(state, *actor);
    if (client == NULL) qa_range_error("Missing local QC client state");
    for (size_t i = 0; i < message_count; i++) retained(client, messages[i]);
  }
}
bool qa_app_sim_quakec_local_messages_stat(qa_app_sim_quakec_local_messages *state, qa_actor_id actor, double index, double *out) {
  local_state *c = client_of(state, actor);
  qa_netquake_message *m = c ? get_key(c, state_key(ST_STAT, (int)index)) : NULL;
  if (m && is(m->kind, "stat")) { *out = m->value; return true; }
  return false;
}
bool qa_app_sim_quakec_local_messages_angles(qa_app_sim_quakec_local_messages *state, qa_actor_id actor, qa_vec3 *out) {
  local_state *c = client_of(state, actor);
  qa_netquake_message *m = c ? get_key(c, state_key(ST_SET_ANGLE, 0)) : NULL;
  if (m && is(m->kind, "set-angle")) { *out = m->angles; return true; }
  return false;
}
bool qa_app_sim_quakec_local_messages_view(qa_app_sim_quakec_local_messages *state, qa_actor_id actor, double *out) {
  local_state *c = client_of(state, actor);
  qa_netquake_message *m = c ? get_key(c, state_key(ST_SET_VIEW, 0)) : NULL;
  if (m && is(m->kind, "set-view")) { *out = m->entity; return true; }
  return false;
}
bool qa_app_sim_quakec_local_messages_view_target(qa_app_sim_quakec_local_messages *state, qa_actor_id actor, qa_actor_id *out) {
  for (size_t i = 0; i < state->view_count; i++) if (qa_same_actor(state->view_actors[i], actor)) { *out = state->view_targets[i]; return true; }
  return false;
}
qa_app_sim_quakec_captured_views qa_app_sim_quakec_local_messages_capture_views(qa_app_sim_quakec_local_messages *state) {
  qa_app_sim_quakec_captured_views out; memset(&out, 0, sizeof out);
  out.has_baseline = state->has_baseline_view; out.baseline = state->baseline_view;
  out.client_count = state->view_count;
  out.clients = out.client_count ? malloc(out.client_count * sizeof *out.clients) : NULL;
  for (size_t i = 0; i < out.client_count; i++) out.clients[i] = (qa_app_sim_quakec_view_entry){state->view_actors[i], state->view_targets[i]};
  return out;
}
void qa_app_sim_quakec_local_messages_restore_views(qa_app_sim_quakec_local_messages *state,
    const qa_actor_id *baseline, const qa_app_sim_quakec_view_entry *clients, size_t client_count) {
  state->has_baseline_view = baseline != NULL;
  if (baseline) state->baseline_view = *baseline;
  state->view_count = 0;
  for (size_t i = 0; i < client_count; i++) {
    qa_actor_id ignored;
    if (!client_of(state, clients[i].actor) || qa_app_sim_quakec_local_messages_view_target(state, clients[i].actor, &ignored))
      qa_range_error("Invalid retained QC view recipient");
    view_set(state, clients[i].actor, clients[i].target);
  }
}
qa_app_sim_quakec_session_state qa_app_sim_quakec_local_messages_session_state(qa_app_sim_quakec_local_messages *state, qa_actor_id actor) {
  local_state *c = client_of(state, actor);
  qa_netquake_message *server = c ? get_key(c, state_key(ST_SERVER_VARS, 0)) : NULL;
  qa_netquake_message *views = c ? get_key(c, state_key(ST_SET_VIEWS, 0)) : NULL;
  qa_netquake_message *sequence = c ? get_key(c, state_key(ST_SEQUENCE, 0)) : NULL;
  qa_app_sim_quakec_session_state out;
  out.server_vars = (server && is(server->kind, "server-vars")) ? (server->text ? server->text : "") : "";
  out.views = (views && is(views->kind, "set-views")) ? views->value : 1;
  out.sequence = (sequence && is(sequence->kind, "sequence")) ? sequence->value : 0;
  out.level_completed = c && get_key(c, state_key(ST_LEVEL_COMPLETED, 0)) != NULL;
  out.back_to_lobby = c && get_key(c, state_key(ST_BACK_TO_LOBBY, 0)) != NULL;
  return out;
}
qa_app_sim_quakec_prompt qa_app_sim_quakec_local_messages_prompt(qa_app_sim_quakec_local_messages *state, qa_actor_id actor) {
  qa_app_sim_quakec_prompt out; memset(&out, 0, sizeof out);
  out.actor = actor;
  local_state *c = client_of(state, actor);
  qa_netquake_message *begin = c ? get_key(c, state_key(ST_PROMPT_BEGIN, 0)) : NULL;
  if (!begin || !is(begin->kind, "prompt-begin") || begin->text == NULL || begin->text[0] == '\0') {
    out.kind = "clear-prompt"; return out;
  }
  out.kind = "prompt"; out.title = begin->text;
  size_t count = 0;
  for (size_t i = 0; i < c->count; i++) if (is(c->values[i].kind, "prompt-choice")) count++;
  out.labels = count ? malloc(count * sizeof *out.labels) : NULL;
  out.impulses = count ? malloc(count * sizeof *out.impulses) : NULL;
  for (size_t i = 0; i < c->count; i++) if (is(c->values[i].kind, "prompt-choice")) {
    out.labels[out.choice_count] = c->values[i].text;
    out.impulses[out.choice_count++] = c->values[i].impulse;
  }
  return out;
}
void qa_app_sim_quakec_prompt_free(qa_app_sim_quakec_prompt *prompt) { free(prompt->labels); free(prompt->impulses); memset(prompt, 0, sizeof *prompt); }
bool qa_app_sim_quakec_local_messages_answer_prompt(qa_app_sim_quakec_local_messages *state, qa_actor_id actor, double impulse) {
  qa_app_sim_quakec_prompt prompt = qa_app_sim_quakec_local_messages_prompt(state, actor);
  bool found = false;
  if (is(prompt.kind, "prompt")) for (size_t i = 0; i < prompt.choice_count; i++) if (prompt.impulses[i] == impulse) found = true;
  qa_app_sim_quakec_prompt_free(&prompt);
  if (!found) return false;
  qa_netquake_message clear; memset(&clear, 0, sizeof clear); clear.kind = "prompt-clear";
  qa_app_sim_quakec_local_messages_receive(state, &clear, 1, &actor, false, NULL);
  return true;
}
bool qa_app_sim_quakec_local_messages_intermission(qa_app_sim_quakec_local_messages *state, qa_actor_id actor) {
  local_state *c = client_of(state, actor);
  return c && get_key(c, state_key(ST_INTERMISSION, 0)) != NULL;
}
const qa_netquake_message *qa_app_sim_quakec_local_messages_presentation(qa_app_sim_quakec_local_messages *state, qa_actor_id actor, size_t *out_count) {
  state->scratch_count = 0;
  local_state *c = client_of(state, actor);
  if (c) for (size_t i = 0; i < c->count; i++) {
    const char *k = c->values[i].kind;
    if (is(k, "skybox") || is(k, "name") || is(k, "colors") || is(k, "frags") || is(k, "ping") || is(k, "social")
        || is(k, "player-info") || is(k, "prompt-begin") || is(k, "prompt-choice") || is(k, "prompt-clear")
        || is(k, "light-style") || is(k, "intermission") || is(k, "finale") || is(k, "cutscene")
        || is(k, "cd-track") || is(k, "pause"))
      state->scratch[state->scratch_count++] = c->values[i];
  }
  if (out_count) *out_count = state->scratch_count;
  return state->scratch;
}
qa_app_sim_quakec_capture qa_app_sim_quakec_local_messages_capture(qa_app_sim_quakec_local_messages *state) {
  qa_app_sim_quakec_capture out; memset(&out, 0, sizeof out);
  out.baseline_count = state->baseline.count;
  out.baseline = out.baseline_count ? malloc(out.baseline_count * sizeof *out.baseline) : NULL;
  if (out.baseline_count) memcpy(out.baseline, state->baseline.values, out.baseline_count * sizeof *out.baseline);
  size_t total = 0;
  for (size_t i = 0; i < state->client_count; i++) total += state->clients[i].count;
  out.storage = total ? malloc(total * sizeof *out.storage) : NULL;
  out.client_count = state->client_count;
  out.clients = out.client_count ? malloc(out.client_count * sizeof *out.clients) : NULL;
  size_t at = 0;
  for (size_t i = 0; i < state->client_count; i++) {
    out.clients[i].actor = state->client_actors[i];
    out.clients[i].messages = total ? &out.storage[at] : NULL;
    out.clients[i].message_count = state->clients[i].count;
    if (out.clients[i].message_count) memcpy(&out.storage[at], state->clients[i].values, out.clients[i].message_count * sizeof *out.storage);
    at += out.clients[i].message_count;
  }
  return out;
}
void qa_app_sim_quakec_capture_free(qa_app_sim_quakec_capture *capture) {
  free(capture->baseline); free(capture->clients); free(capture->storage); memset(capture, 0, sizeof *capture);
}
void qa_app_sim_quakec_local_messages_restore(qa_app_sim_quakec_local_messages *state,
    const qa_netquake_message *baseline, size_t baseline_count,
    const qa_app_sim_quakec_captured_client *clients, size_t client_count) {
  state->baseline.count = 0; state->client_count = 0; state->has_baseline_view = false; state->view_count = 0;
  for (size_t i = 0; i < baseline_count; i++) retained(&state->baseline, baseline[i]);
  for (size_t i = 0; i < client_count; i++) {
    if (client_of(state, clients[i].actor)) qa_range_error("Duplicate restored local QC client");
    if (state->client_count >= MAX_CLIENTS) qa_range_error("Out of memory");
    state->client_actors[state->client_count] = clients[i].actor;
    local_state *c = &state->clients[state->client_count++]; c->count = 0;
    for (size_t j = 0; j < clients[i].message_count; j++) retained(c, clients[i].messages[j]);
  }
}
bool qa_app_sim_quakec_local_view(qa_actor_id actor, qa_app_sim_quakec_local_messages *state,
    qa_app_sim_quakec_local_view_source source, qa_app_sim_player_view *out) {
  bool intermission = qa_app_sim_quakec_local_messages_intermission(state, actor);
  qa_actor_id target; bool has_target = qa_app_sim_quakec_local_messages_view_target(state, actor, &target);
  if (!intermission && (!has_target || qa_same_actor(target, actor))) return false;
  qa_vec3 origin, angles;
  if (!source.read(source.ctx, has_target ? target : actor, &origin, &angles)) return false;
  qa_vec3 offset = intermission ? (qa_vec3){0, 0, 0} : source.offset(source.ctx, actor);
  memset(out, 0, sizeof *out);
  out->origin = (qa_vec3){origin.x + offset.x, origin.y + offset.y, origin.z};
  out->view_height = offset.z;
  qa_vec3 local_angles;
  out->angles = qa_app_sim_quakec_local_messages_angles(state, actor, &local_angles) ? local_angles : angles;
  return true;
}
static qa_app_sim_presentation_event event_of(const char *kind, const char *event_kind) {
  qa_app_sim_presentation_event e; memset(&e, 0, sizeof e); e.kind = kind; e.event_kind = event_kind; return e;
}
void qa_app_sim_present_quakec_local_message(qa_netquake_message message, const qa_actor_id *target,
    qa_app_sim_quakec_local_messages *state, qa_app_sim_quakec_local_message_host host) {
  qa_actor_id single_storage; memset(&single_storage, 0, sizeof single_storage);
  if (target) single_storage = *target;
  const qa_actor_id *single = &single_storage;
  const qa_actor_id *recipients = target ? single : host.recipients;
  size_t recipient_count = target ? 1 : host.recipient_count;
  const char *k = message.kind;
  if (is(k, "server-vars") || is(k, "set-views") || is(k, "sequence") || is(k, "nop") || is(k, "set-view") || is(k, "temporary-entity")) return;
  if (is(k, "level-completed") || is(k, "back-to-lobby")) { host.session(host.ctx, k, target); return; }
  if (is(k, "prompt-begin") || is(k, "prompt-choice") || is(k, "prompt-clear")) {
    for (size_t i = 0; i < recipient_count; i++) {
      qa_app_sim_quakec_prompt p = qa_app_sim_quakec_local_messages_prompt(state, recipients[i]);
      host.prompt(host.ctx, p);
    }
    return;
  }
  if (is(k, "stat")) {
    if (message.index == 12) {
      qa_app_sim_presentation_event e = event_of("q1", "monster-total"); e.value = message.value;
      host.emit(host.ctx, e, NULL);
    }
    return;
  }
  if (is(k, "name") || is(k, "colors") || is(k, "frags") || is(k, "ping") || is(k, "social") || is(k, "player-info")) {
    host.client_metadata(host.ctx, message, target); return;
  }
  if (is(k, "skybox")) { host.sky(host.ctx, message.text, target); return; }
  if (is(k, "raw-print") || is(k, "chat") || is(k, "botchat")) {
    qa_app_sim_presentation_event e = event_of("print", NULL); e.value = 2; e.value_text = message.text;
    host.message(host.ctx, e, target); return;
  }
  if (is(k, "print") || is(k, "center-print") || is(k, "stufftext")) {
    qa_app_sim_presentation_event e = event_of(is(k, "stufftext") ? "command-text" : is(k, "print") ? "print" : "center-print", NULL);
    e.value = 2; e.value_text = message.text;
    host.message(host.ctx, e, target); return;
  }
  if (is(k, "disconnect")) {
    qa_app_sim_presentation_event e = event_of("disconnect", NULL); e.value_text = "Source disconnected client";
    host.message(host.ctx, e, target); return;
  }
  if (is(k, "set-angle")) { for (size_t i = 0; i < recipient_count; i++) host.angles(host.ctx, recipients[i], message.angles); return; }
  if (is(k, "light-style")) {
    qa_app_sim_presentation_event e = event_of("q1", "lightstyle"); e.value = message.index; e.value_text = message.str_value ? message.str_value : message.text;
    host.emit(host.ctx, e, NULL); return;
  }
  if (is(k, "cd-track")) { host.music(host.ctx, message.track); return; }
  if (is(k, "pause")) { host.pause(host.ctx, message.paused); return; }
  if (is(k, "fog")) { host.fog(host.ctx, message, target); return; }
  if (is(k, "bonus-flash")) {
    for (size_t i = 0; i < recipient_count; i++) {
      qa_vec3 origin, angles; host.camera(host.ctx, recipients[i], &origin, &angles);
      qa_app_sim_presentation_event e = event_of("q1", "effect"); e.name = "pickup";
      e.actor = recipients[i]; e.angles = origin; e.value = 1;
      host.emit(host.ctx, e, &recipients[i]);
    }
    return;
  }
  if (is(k, "achievement")) {
    qa_app_sim_presentation_event e = event_of("q1", "achievement");
    e.has_player = target != NULL; if (target) e.player = *target;
    e.value_text = message.text;
    host.emit(host.ctx, e, target); return;
  }
  if (is(k, "spawned-monster")) {
    for (size_t i = 0; i < recipient_count; i++) {
      double total = 0; qa_app_sim_quakec_local_messages_stat(state, recipients[i], 12, &total);
      qa_app_sim_presentation_event e = event_of("q1", "monster-total"); e.value = total;
      host.emit(host.ctx, e, &recipients[i]);
    }
    return;
  }
  if (is(k, "local-sound")) {
    for (size_t i = 0; i < recipient_count; i++) {
      qa_actor_id listener; bool has = qa_app_sim_quakec_local_messages_view_target(state, recipients[i], &listener);
      qa_app_sim_presentation_event e = event_of("q1", "sound");
      e.actor = has ? listener : recipients[i]; e.value_text = host.sound(host.ctx, message.index);
      e.value = -1;
      host.emit(host.ctx, e, &recipients[i]);
    }
    return;
  }
  if (is(k, "damage")) {
    qa_app_sim_presentation_event e = event_of("q1-damage", NULL);
    e.value = message.armor; e.color = message.blood; e.angles = message.source;
    host.message(host.ctx, e, target); return;
  }
  if (is(k, "particle")) {
    qa_app_sim_presentation_event e = event_of("q1-particle", NULL);
    e.angles = message.origin; e.value = message.count;
    host.message(host.ctx, e, target); return;
  }
  if (is(k, "killed-monster") || is(k, "found-secret")) {
    bool secret = is(k, "found-secret");
    for (size_t i = 0; i < recipient_count; i++) {
      double total = 0, found = 0;
      qa_app_sim_quakec_local_messages_stat(state, recipients[i], secret ? 11 : 12, &total);
      qa_app_sim_quakec_local_messages_stat(state, recipients[i], secret ? 13 : 14, &found);
      qa_app_sim_presentation_event e = event_of("q1", secret ? "secret" : "monster-killed");
      e.actor = recipients[i]; e.value = total; e.color = found;
      host.emit(host.ctx, e, &recipients[i]);
    }
    return;
  }
  if (is(k, "intermission")) {
    for (size_t i = 0; i < recipient_count; i++) {
      qa_vec3 origin, angles; host.camera(host.ctx, recipients[i], &origin, &angles);
      qa_app_sim_presentation_event e = event_of("q1-level", "intermission");
      e.angles = angles; e.name = host.map; e.value = host.seconds + 5;
      host.emit(host.ctx, e, &recipients[i]);
    }
    return;
  }
  if (is(k, "finale") || is(k, "cutscene")) {
    qa_app_sim_presentation_event e = event_of("q1", "finale");
    e.value_text = message.text; e.value = is(k, "finale") ? 4 : 1;
    host.emit(host.ctx, e, NULL); return;
  }
  if (is(k, "sell-screen")) {
    qa_app_sim_presentation_event e = event_of("command-text", NULL); e.value_text = "help\n";
    host.message(host.ctx, e, target); return;
  }
  if (is(k, "sound") || is(k, "static-sound")) {
    const char *path = host.sound(host.ctx, message.index);
    if (is(k, "static-sound")) {
      qa_app_sim_presentation_event e = event_of("q1", "ambient");
      e.value_text = path; e.angles = message.origin; e.value = message.volume / 255; e.color = message.attenuation;
      host.emit(host.ctx, e, NULL);
    } else {
      static const char *const channels[] = {"auto", "weapon", "voice", "item", "body", "5", "6", "7"};
      int channel = (int)message.channel;
      if (channel < 0 || channel > 7) qa_range_error("Invalid local QC sound channel");
      qa_app_sim_presentation_event e = event_of("q1", "sound");
      e.actor = host.actor(host.ctx, message.entity); e.value_text = path; e.name = channels[channel];
      e.angles = message.origin; e.value = message.volume / 255; e.color = message.attenuation;
      host.emit(host.ctx, e, NULL);
    }
    return;
  }
  if (is(k, "stop-sound")) {
    qa_app_sim_presentation_event e = event_of("q1", "stop-sound");
    e.actor = host.actor(host.ctx, message.entity); e.value = message.channel;
    host.emit(host.ctx, e, NULL); return;
  }
  if (is(k, "static")) {
    qa_app_sim_presentation_event e = event_of("q1", "static-model");
    e.value_text = host.model(host.ctx, message.state.base.model_index);
    e.value = message.state.base.frame; e.angles = message.state.base.angles;
    host.emit(host.ctx, e, NULL); return;
  }
  qa_range_error("Unsupported local QuakeC service %s", k ? k : "(null)");
}
