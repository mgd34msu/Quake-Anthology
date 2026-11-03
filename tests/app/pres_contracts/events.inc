/* pres_contracts part: SimulationPresentationEvent (simulation/types.ts) as
 * one owned, copyable C type. Every SourcePresentationEvent kind is built by
 * a producer whose strings and arrays live in its own heap blocks, received
 * into a PresentationState-sized store (sequence renumbered as TS
 * receivePresentation does), the producer's blocks are scribbled and freed,
 * and the stored values are read back. Expected values are the TS output of
 * build/agents/pres-contracts/tmp/events_oracle.ts under bun (PresentationState
 * receivePresentation + takePresentation on the same 27 events). Included by
 * tests/app/test_pres_contracts.c. */
#include "qa/app/bootstrap/simulation/events.h"
#include "qa/app/bootstrap/simulation/q3/host.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/content/composition/q1/types.h"
#include "qa/content/composition/q2/types.h"
#include "qa/content/q1/base/rules.h"
#include "qa/content/q1/foundation/types.h"
#include "qa/content/q2/base/player/types.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/foundation/weapons/types.h"
#include "qa/content/q2/multiplayer/ctf/types.h"
#include "qa/content/q2/rerelease/campaign.h"
#include "qa/content/q2/rerelease/types.h"

#define PRES_EVENT_CAPACITY 32768 /* TS PRESENTATION_EVENT_CAPACITY */

/* Producer-owned blocks: every string and array a producer hands over. */
typedef struct pres_block { void *data; size_t size; } pres_block;
static pres_block pres_blocks[512];
static size_t pres_block_count;
static void *pres_keep(const void *data, size_t size) {
  void *copy = malloc(size ? size : 1);
  if (!copy) abort();
  if (size) memcpy(copy, data, size);
  pres_blocks[pres_block_count].data = copy;
  pres_blocks[pres_block_count++].size = size;
  return copy;
}
static const char *pres_text(const char *text) { return pres_keep(text, strlen(text) + 1); }
static void pres_scribble_producers(void) {
  for (size_t i = 0; i < pres_block_count; i++) {
    memset(pres_blocks[i].data, 'X', pres_blocks[i].size);
    free(pres_blocks[i].data);
  }
  pres_block_count = 0;
}
static qa_actor_id pres_actor(int64_t slot, int64_t generation, const void *token) {
  qa_actor_id actor; memset(&actor, 0, sizeof actor);
  actor.session.name = pres_text("pres-contracts");
  actor.session.token = token;
  actor.slot = slot; actor.generation = generation;
  return actor;
}
static bool pres_is_actor(qa_actor_id actor, int64_t slot, int64_t generation, const void *token) {
  return actor.slot == slot && actor.generation == generation && actor.session.token == token
      && actor.session.name != NULL && strcmp(actor.session.name, "pres-contracts") == 0;
}
static bool pres_vec(qa_vec3 v, double x, double y, double z) { return v.x == x && v.y == y && v.z == z; }

/* A PresentationState-sized ring of owned events. */
typedef struct pres_store {
  qa_app_sim_presentation_event *events;
  size_t head, count;
  double sequence;
} pres_store;
static void pres_store_init(pres_store *store) {
  store->events = calloc(PRES_EVENT_CAPACITY, sizeof *store->events);
  if (!store->events) abort();
  store->head = store->count = 0; store->sequence = 0;
}
/* TS receivePresentation: record({...source, sequence: this.presentationSequence++}). */
static bool pres_store_receive(pres_store *store, const qa_app_sim_presentation_event *source) {
  if (store->count == PRES_EVENT_CAPACITY) return false;
  qa_app_sim_presentation_event stamped = *source;
  stamped.sequence = store->sequence++;
  qa_app_sim_presentation_event *slot = &store->events[(store->head + store->count) % PRES_EVENT_CAPACITY];
  if (!qa_app_sim_presentation_event_copy(slot, &stamped)) abort();
  store->count++;
  return true;
}
static void pres_store_destroy(pres_store *store) {
  for (size_t i = 0; i < store->count; i++)
    qa_app_sim_presentation_event_release(&store->events[(store->head + i) % PRES_EVENT_CAPACITY]);
  free(store->events);
}
static qa_app_sim_presentation_event pres_stamp(const char *kind) {
  qa_app_sim_presentation_event event; memset(&event, 0, sizeof event);
  event.kind = pres_text(kind);
  event.content = pres_text("q1:id1:base:1");
  event.seconds = 2.5;
  return event;
}

static size_t pres_build_events(qa_app_sim_presentation_event *out, const void *token) {
  size_t n = 0;
  qa_actor_id a = pres_actor(3, 1, token), b = pres_actor(7, 2, token);
  qa_app_sim_presentation_event e;

  e = pres_stamp("presentation-owner");
  e.presentation_owner.kind = pres_text("refreshed");
  e.presentation_owner.owner.provider = pres_text("mod:alpha"); e.presentation_owner.owner.generation = 4;
  out[n++] = e;

  e = pres_stamp("debug-graph");
  e.debug_graph.kind = pres_text("sample"); e.debug_graph.value = 3.5; e.debug_graph.color = 7;
  out[n++] = e;

  { /* q1 message with args and parts */
    qa_q1_msg_arg args[2]; memset(args, 0, sizeof args);
    args[0].text = pres_text("name"); args[1].is_number = true; args[1].number = 12;
    qa_q1_msg_arg part_args[2]; memset(part_args, 0, sizeof part_args);
    part_args[0].text = pres_text("x"); part_args[1].is_number = true; part_args[1].number = 4;
    qa_q1_message_part parts[2]; memset(parts, 0, sizeof parts);
    parts[0].text = pres_text("part one"); parts[0].args = pres_keep(part_args, sizeof part_args); parts[0].arg_count = 2;
    parts[1].text = pres_text("part two");
    qa_q1_event q; memset(&q, 0, sizeof q);
    q.kind = pres_text("message"); q.has_player = true; q.player = a; q.text = pres_text("$qc_hello"); q.center = true;
    q.args = pres_keep(args, sizeof args); q.arg_count = 2;
    q.parts = pres_keep(parts, sizeof parts); q.part_count = 2;
    e = pres_stamp("q1"); e.q1 = pres_keep(&q, sizeof q);
    e.has_recipient = true; e.recipient = a; e.has_source_entity = true; e.source_entity = 3;
    out[n++] = e;
  }
  { /* q1 sound */
    qa_q1_event q; memset(&q, 0, sizeof q);
    q.kind = pres_text("sound"); q.has_actor = true; q.actor = b; q.has_origin = true; q.origin = (qa_vec3){1, 2, 3};
    q.path = pres_text("weapons/rocket.wav"); q.sound_channel = pres_text("weapon"); q.attenuation = 1; q.volume = 0.5;
    e = pres_stamp("q1"); e.q1 = pres_keep(&q, sizeof q);
    out[n++] = e;
  }
  e = pres_stamp("q1-sky"); e.q1_sky.kind = pres_text("skybox"); e.q1_sky.name = pres_text("unit1_");
  out[n++] = e;
  e = pres_stamp("q1-client"); e.q1_client.kind = pres_text("name"); e.q1_client.slot = 2; e.q1_client.text = pres_text("Ranger");
  out[n++] = e;
  e = pres_stamp("q1-client"); e.q1_client.kind = pres_text("frags"); e.q1_client.slot = 2; e.q1_client.value = -3;
  out[n++] = e;
  e = pres_stamp("q1-session"); e.q1_session.kind = pres_text("back-to-lobby");
  out[n++] = e;
  e = pres_stamp("q1-fog"); e.q1_fog.kind = pres_text("transition"); e.q1_fog.has_player = true; e.q1_fog.player = a;
  e.q1_fog.transition.target.density = 0.05; e.q1_fog.transition.target.color = (qa_vec3){0.3, 0.2, 0.1};
  e.q1_fog.transition.start = 1; e.q1_fog.transition.duration = 2; e.q1_fog.sky_factor = 0.5;
  out[n++] = e;
  e = pres_stamp("music"); e.music.kind = pres_text("cd-track"); e.music.track = 4;
  out[n++] = e;
  e = pres_stamp("music"); e.music.kind = pres_text("pause"); e.music.paused = true;
  out[n++] = e;
  { /* q1-composition prompt */
    qa_comp_q1_prompt_choice choices[2] = {{NULL, 101}, {NULL, 102}};
    choices[0].label = pres_text("Red"); choices[1].label = pres_text("Blue");
    qa_comp_q1_event c; memset(&c, 0, sizeof c);
    c.kind = QA_COMP_Q1_EVENT_PROMPT; c.actor = a; c.title = pres_text("Choose");
    c.choices = pres_keep(choices, sizeof choices); c.choice_count = 2;
    e = pres_stamp("q1-composition"); e.q1_composition = pres_keep(&c, sizeof c);
    out[n++] = e;
  }
  { /* q1-composition client */
    qa_comp_userinfo_entry info[2];
    info[0].key = pres_text("team"); info[0].value = pres_text("red");
    info[1].key = pres_text("skin"); info[1].value = pres_text("base");
    qa_comp_q1_event c; memset(&c, 0, sizeof c);
    c.kind = QA_COMP_Q1_EVENT_CLIENT; c.client.actor = b; c.client.slot = 1; c.client.name = pres_text("Visitor");
    c.client.frags = 9; c.client.shirt = 4; c.client.pants = 12; c.client.team = 5; c.client.no_target = true;
    c.client.userinfo = pres_keep(info, sizeof info); c.client.userinfo_count = 2;
    e = pres_stamp("q1-composition"); e.q1_composition = pres_keep(&c, sizeof c);
    out[n++] = e;
  }
  { /* q1-level finale */
    qa_q1_intermission_result *r = pres_keep(&(qa_q1_intermission_result){0}, sizeof *r);
    r->kind = pres_text("finale"); strcpy(r->text, "The end"); r->track = 2;
    e = pres_stamp("q1-level"); e.q1_level = r;
    out[n++] = e;
  }
  { /* q2 model */
    qa_q2_presentation_event q; memset(&q, 0, sizeof q);
    q.kind = pres_text("model"); q.has_actor = true; q.actor = a; q.path = pres_text("models/monsters/tank/tris.md2");
    q.attached_models[0] = pres_text("a.md2"); q.attached_models[1] = pres_text("b.md2");
    q.frame = 3; q.old_frame = 2; q.scale = 1; q.alpha = 1; q.visible = true;
    e = pres_stamp("q2"); e.q2 = pres_keep(&q, sizeof q);
    out[n++] = e;
  }
  { /* q2 looping sound */
    qa_q2_presentation_event q; memset(&q, 0, sizeof q);
    q.kind = pres_text("sound"); q.has_actor = true; q.actor = b; q.path = pres_text("world/amb10.wav");
    q.origin = (qa_vec3){4, 5, 6}; q.volume = 1; q.attenuation = 3; q.loop = pres_text("start");
    q.has_loop_owner = true; q.loop_owner = pres_text("mod:loop");
    e = pres_stamp("q2"); e.q2 = pres_keep(&q, sizeof q);
    out[n++] = e;
  }
  { /* q2-weapon beam, actor null */
    qa_q2w_event w; memset(&w, 0, sizeof w);
    w.kind = QA_Q2W_EVENT_BEAM; w.effect = pres_text("rail"); w.end = (qa_vec3){64, 0, 8}; w.duration = 0.1;
    e = pres_stamp("q2-weapon"); e.q2_weapon = pres_keep(&w, sizeof w);
    out[n++] = e;
  }
  { /* q2-weapon view-weapon */
    qa_q2w_event w; memset(&w, 0, sizeof w);
    w.kind = QA_Q2W_EVENT_VIEW_WEAPON; w.actor = pres_keep(&a, sizeof a); w.weapon = pres_text("railgun");
    w.model = pres_text("models/weapons/v_rail/tris.md2"); w.player_model = 7; w.frame = 5; w.rate = 10;
    w.kick_angles = (qa_vec3){-2, 0, 0};
    e = pres_stamp("q2-weapon"); e.q2_weapon = pres_keep(&w, sizeof w);
    out[n++] = e;
  }
  e = pres_stamp("view-reset"); e.view_reset.reason = pres_text("teleport"); e.view_reset.actor = b;
  e.view_reset.angles = (qa_vec3){0, 90, 0};
  out[n++] = e;
  { /* q2-composition ctf scoreboard */
    qa_q2_ctf_event ctf; memset(&ctf, 0, sizeof ctf);
    ctf.kind = pres_text("scoreboard"); ctf.actor = a; ctf.layout = pres_text("xv 0 yv 8 string \"CTF\"");
    qa_q2_composition_event c; memset(&c, 0, sizeof c);
    c.kind = QA_Q2_COMPOSITION_EVENT_CTF; c.ctf = pres_keep(&ctf, sizeof ctf);
    e = pres_stamp("q2-composition"); e.q2_composition = pres_keep(&c, sizeof c);
    out[n++] = e;
  }
  { /* q2-rerelease localized-print, actor null */
    const char *args[2]; args[0] = pres_text("one"); args[1] = pres_text("two");
    qa_q2_rerelease_event r; memset(&r, 0, sizeof r);
    r.kind = pres_text("localized-print"); r.level = pres_text("high"); r.text = pres_text("$g_pc_kill");
    r.args = pres_keep(args, sizeof args); r.arg_count = 2;
    e = pres_stamp("q2-rerelease"); e.q2_rerelease = pres_keep(&r, sizeof r);
    out[n++] = e;
  }
  { /* q2-rerelease end-of-unit */
    qa_q2_rerelease_level_entry level; memset(&level, 0, sizeof level);
    level.map = (char *)pres_text("base1"); level.name = (char *)pres_text("Outer Base");
    level.visit_order = 1; level.total_secrets = 3; level.found_secrets = 1; level.total_monsters = 30;
    level.killed_monsters = 28; level.time = 412;
    qa_q2_rerelease_event r; memset(&r, 0, sizeof r);
    r.kind = pres_text("end-of-unit"); r.levels = pres_keep(&level, sizeof level); r.level_count = 1; r.button_time = 9;
    e = pres_stamp("q2-rerelease"); e.q2_rerelease = pres_keep(&r, sizeof r);
    out[n++] = e;
  }
  { /* q2-player inventory */
    qa_inventory_entry entry; memset(&entry, 0, sizeof entry);
    entry.item = pres_text("q2:shells"); entry.count = 25; entry.capacity = 100;
    qa_q2_player_item_label label; label.item = pres_text("q2:shells"); label.name = pres_text("Shells");
    qa_q2_player_event p; memset(&p, 0, sizeof p);
    p.kind = pres_text("inventory"); p.actor = a; p.entries = pres_keep(&entry, sizeof entry); p.entry_count = 1;
    p.has_visible = true; p.visible = true; p.has_selected = true; p.selected = pres_text("q2:shells");
    p.labels = pres_keep(&label, sizeof label); p.label_count = 1;
    e = pres_stamp("q2-player"); e.q2_player = pres_keep(&p, sizeof p);
    out[n++] = e;
  }
  { /* q2-player scoreboard */
    qa_q2_score_row row; memset(&row, 0, sizeof row);
    row.slot = 0; row.name = pres_text("Grunt"); row.score = 4; row.ping = 33; row.minutes = 2;
    qa_q2_player_event p; memset(&p, 0, sizeof p);
    p.kind = pres_text("scoreboard"); p.actor = b; p.rows = pres_keep(&row, sizeof row); p.row_count = 1;
    p.has_killer = true; p.killer = a; p.reliable = true;
    e = pres_stamp("q2-player"); e.q2_player = pres_keep(&p, sizeof p);
    out[n++] = e;
  }
  e = pres_stamp("q3-character"); e.q3_character.actor = b; e.q3_character.sequence = 6;
  e.q3_character.time_milliseconds = 1500; e.q3_character.event = 21; e.q3_character.parameter = 3;
  out[n++] = e;
  { /* q3-source sound */
    qa_app_q3_source_event s; memset(&s, 0, sizeof s);
    s.kind = QA_APP_Q3_EVENT_SOUND; s.actor = a; s.origin = (qa_vec3){8, 9, 10}; s.velocity = (qa_vec3){0, 0, -1};
    s.path = pres_text("sound/weapons/rocket/rocklf1a.wav"); s.channel = 2; s.volume = 1;
    e = pres_stamp("q3-source"); e.q3_source = pres_keep(&s, sizeof s);
    out[n++] = e;
  }
  { /* q3-source server-command */
    qa_app_q3_source_event s; memset(&s, 0, sizeof s);
    s.kind = QA_APP_Q3_EVENT_SERVER_COMMAND; s.client = 1; s.text = pres_text("cp \"Fight!\"");
    e = pres_stamp("q3-source"); e.q3_source = pres_keep(&s, sizeof s);
    out[n++] = e;
  }
  return n;
}

static void pres_check_events(const qa_app_sim_presentation_event *ev, const void *token) {
  for (size_t i = 0; i < 27; i++) {
    CHECK(ev[i].sequence == (double)i);
    CHECK_STR(ev[i].content, "q1:id1:base:1");
    CHECK(ev[i].seconds == 2.5);
    CHECK(ev[i].storage != NULL);
    CHECK(qa_app_sim_source_presentation_kind_known(ev[i].kind));
  }
  CHECK_STR(ev[0].kind, "presentation-owner");
  CHECK_STR(ev[0].presentation_owner.kind, "refreshed");
  CHECK_STR(ev[0].presentation_owner.owner.provider, "mod:alpha");
  CHECK(ev[0].presentation_owner.owner.generation == 4);
  CHECK(!ev[0].has_owner && !ev[0].has_recipient && !ev[0].has_source_entity);
  CHECK_STR(ev[1].debug_graph.kind, "sample");
  CHECK(ev[1].debug_graph.value == 3.5 && ev[1].debug_graph.color == 7);
  const qa_q1_event *m = ev[2].q1;
  CHECK_STR(ev[2].kind, "q1");
  CHECK_STR(m->kind, "message");
  CHECK(m->has_player && pres_is_actor(m->player, 3, 1, token));
  CHECK_STR(m->text, "$qc_hello");
  CHECK(m->center);
  CHECK(m->arg_count == 2 && !m->args[0].is_number && m->args[1].is_number && m->args[1].number == 12);
  CHECK_STR(m->args[0].text, "name");
  CHECK(m->part_count == 2);
  CHECK_STR(m->parts[0].text, "part one");
  CHECK(m->parts[0].arg_count == 2 && m->parts[0].args[1].is_number && m->parts[0].args[1].number == 4);
  CHECK_STR(m->parts[0].args[0].text, "x");
  CHECK_STR(m->parts[1].text, "part two");
  CHECK(m->parts[1].args == NULL && m->parts[1].arg_count == 0);
  CHECK(ev[2].has_recipient && pres_is_actor(ev[2].recipient, 3, 1, token));
  CHECK(ev[2].has_source_entity && ev[2].source_entity == 3);
  const qa_q1_event *sound = ev[3].q1;
  CHECK_STR(sound->kind, "sound");
  CHECK(sound->has_actor && pres_is_actor(sound->actor, 7, 2, token));
  CHECK(sound->has_origin && pres_vec(sound->origin, 1, 2, 3));
  CHECK_STR(sound->path, "weapons/rocket.wav");
  CHECK_STR(sound->sound_channel, "weapon");
  CHECK(sound->attenuation == 1 && sound->volume == 0.5);
  CHECK_STR(ev[4].q1_sky.kind, "skybox");
  CHECK_STR(ev[4].q1_sky.name, "unit1_");
  CHECK_STR(ev[5].q1_client.kind, "name");
  CHECK(ev[5].q1_client.slot == 2);
  CHECK_STR(ev[5].q1_client.text, "Ranger");
  CHECK_STR(ev[6].q1_client.kind, "frags");
  CHECK(ev[6].q1_client.slot == 2 && ev[6].q1_client.value == -3);
  CHECK_STR(ev[7].q1_session.kind, "back-to-lobby");
  CHECK_STR(ev[8].q1_fog.kind, "transition");
  CHECK(ev[8].q1_fog.has_player && pres_is_actor(ev[8].q1_fog.player, 3, 1, token));
  CHECK(ev[8].q1_fog.transition.previous.density == 0 && ev[8].q1_fog.transition.target.density == 0.05);
  CHECK(pres_vec(ev[8].q1_fog.transition.target.color, 0.3, 0.2, 0.1));
  CHECK(ev[8].q1_fog.transition.start == 1 && ev[8].q1_fog.transition.duration == 2 && ev[8].q1_fog.sky_factor == 0.5);
  CHECK_STR(ev[9].music.kind, "cd-track");
  CHECK(ev[9].music.track == 4);
  CHECK_STR(ev[10].music.kind, "pause");
  CHECK(ev[10].music.paused);
  const qa_comp_q1_event *prompt = ev[11].q1_composition;
  CHECK(prompt->kind == QA_COMP_Q1_EVENT_PROMPT && pres_is_actor(prompt->actor, 3, 1, token));
  CHECK_STR(prompt->title, "Choose");
  CHECK(prompt->choice_count == 2 && prompt->choices[0].impulse == 101 && prompt->choices[1].impulse == 102);
  CHECK_STR(prompt->choices[0].label, "Red");
  CHECK_STR(prompt->choices[1].label, "Blue");
  const qa_comp_q1_event *client = ev[12].q1_composition;
  CHECK(client->kind == QA_COMP_Q1_EVENT_CLIENT && pres_is_actor(client->client.actor, 7, 2, token));
  CHECK(client->client.slot == 1 && client->client.frags == 9 && client->client.shirt == 4 && client->client.pants == 12);
  CHECK(client->client.team == 5 && !client->client.observer && client->client.no_target);
  CHECK_STR(client->client.name, "Visitor");
  CHECK(client->client.userinfo_count == 2);
  CHECK_STR(client->client.userinfo[0].key, "team");
  CHECK_STR(client->client.userinfo[0].value, "red");
  CHECK_STR(client->client.userinfo[1].key, "skin");
  CHECK_STR(client->client.userinfo[1].value, "base");
  CHECK_STR(ev[13].q1_level->kind, "finale");
  CHECK_STR(ev[13].q1_level->text, "The end");
  CHECK(ev[13].q1_level->track == 2);
  const qa_q2_presentation_event *model = ev[14].q2;
  CHECK_STR(model->kind, "model");
  CHECK(model->has_actor && pres_is_actor(model->actor, 3, 1, token));
  CHECK_STR(model->path, "models/monsters/tank/tris.md2");
  CHECK_STR(model->attached_models[0], "a.md2");
  CHECK_STR(model->attached_models[1], "b.md2");
  CHECK(model->attached_models[2] == NULL);
  CHECK(model->frame == 3 && model->old_frame == 2 && model->scale == 1 && model->alpha == 1 && model->visible);
  const qa_q2_presentation_event *loop = ev[15].q2;
  CHECK_STR(loop->kind, "sound");
  CHECK(pres_is_actor(loop->actor, 7, 2, token) && pres_vec(loop->origin, 4, 5, 6));
  CHECK_STR(loop->path, "world/amb10.wav");
  CHECK(loop->channel == 0 && loop->volume == 1 && loop->attenuation == 3 && !loop->reliable);
  CHECK_STR(loop->loop, "start");
  CHECK(loop->has_loop_owner);
  CHECK_STR(loop->loop_owner, "mod:loop");
  const qa_q2w_event *beam = ev[16].q2_weapon;
  CHECK(beam->kind == QA_Q2W_EVENT_BEAM && beam->actor == NULL);
  CHECK_STR(beam->effect, "rail");
  CHECK(pres_vec(beam->start, 0, 0, 0) && pres_vec(beam->end, 64, 0, 8) && beam->duration == 0.1);
  const qa_q2w_event *view = ev[17].q2_weapon;
  CHECK(view->kind == QA_Q2W_EVENT_VIEW_WEAPON && view->actor != NULL && pres_is_actor(*view->actor, 3, 1, token));
  CHECK_STR(view->weapon, "railgun");
  CHECK_STR(view->model, "models/weapons/v_rail/tris.md2");
  CHECK(view->player_model == 7 && view->frame == 5 && view->skin == 0 && view->rate == 10);
  CHECK(pres_vec(view->kick_origin, 0, 0, 0) && pres_vec(view->kick_angles, -2, 0, 0));
  CHECK_STR(ev[18].kind, "view-reset");
  CHECK_STR(ev[18].view_reset.reason, "teleport");
  CHECK(pres_is_actor(ev[18].view_reset.actor, 7, 2, token) && pres_vec(ev[18].view_reset.angles, 0, 90, 0));
  const qa_q2_composition_event *ctf = ev[19].q2_composition;
  CHECK(ctf->kind == QA_Q2_COMPOSITION_EVENT_CTF && ctf->ctf != NULL && ctf->lmctf == NULL);
  CHECK_STR(ctf->ctf->kind, "scoreboard");
  CHECK(pres_is_actor(ctf->ctf->actor, 3, 1, token));
  CHECK_STR(ctf->ctf->layout, "xv 0 yv 8 string \"CTF\"");
  const qa_q2_rerelease_event *print = ev[20].q2_rerelease;
  CHECK_STR(print->kind, "localized-print");
  CHECK(!print->has_actor);
  CHECK_STR(print->level, "high");
  CHECK_STR(print->text, "$g_pc_kill");
  CHECK(print->arg_count == 2);
  CHECK_STR(print->args[0], "one");
  CHECK_STR(print->args[1], "two");
  const qa_q2_rerelease_event *unit = ev[21].q2_rerelease;
  CHECK_STR(unit->kind, "end-of-unit");
  CHECK(unit->level_count == 1 && unit->button_time == 9);
  CHECK_STR(unit->levels[0].map, "base1");
  CHECK_STR(unit->levels[0].name, "Outer Base");
  CHECK(unit->levels[0].visit_order == 1 && unit->levels[0].total_secrets == 3 && unit->levels[0].found_secrets == 1);
  CHECK(unit->levels[0].total_monsters == 30 && unit->levels[0].killed_monsters == 28 && unit->levels[0].time == 412);
  const qa_q2_player_event *inventory = ev[22].q2_player;
  CHECK_STR(inventory->kind, "inventory");
  CHECK(pres_is_actor(inventory->actor, 3, 1, token) && inventory->entry_count == 1);
  CHECK_STR(inventory->entries[0].item, "q2:shells");
  CHECK(inventory->entries[0].count == 25 && inventory->entries[0].capacity == 100);
  CHECK(inventory->has_visible && inventory->visible && inventory->has_selected);
  CHECK_STR(inventory->selected, "q2:shells");
  CHECK(inventory->label_count == 1);
  CHECK_STR(inventory->labels[0].item, "q2:shells");
  CHECK_STR(inventory->labels[0].name, "Shells");
  const qa_q2_player_event *board = ev[23].q2_player;
  CHECK_STR(board->kind, "scoreboard");
  CHECK(pres_is_actor(board->actor, 7, 2, token) && board->row_count == 1);
  CHECK_STR(board->rows[0].name, "Grunt");
  CHECK(board->rows[0].slot == 0 && board->rows[0].score == 4 && board->rows[0].ping == 33 && board->rows[0].minutes == 2);
  CHECK(!board->rows[0].spectator && board->has_killer && pres_is_actor(board->killer, 3, 1, token) && board->reliable);
  CHECK(pres_is_actor(ev[24].q3_character.actor, 7, 2, token));
  CHECK(ev[24].q3_character.sequence == 6 && ev[24].q3_character.time_milliseconds == 1500);
  CHECK(ev[24].q3_character.event == 21 && ev[24].q3_character.parameter == 3);
  const qa_app_q3_source_event *q3sound = ev[25].q3_source;
  CHECK(q3sound->kind == QA_APP_Q3_EVENT_SOUND && pres_is_actor(q3sound->actor, 3, 1, token));
  CHECK(pres_vec(q3sound->origin, 8, 9, 10) && pres_vec(q3sound->velocity, 0, 0, -1));
  CHECK_STR(q3sound->path, "sound/weapons/rocket/rocklf1a.wav");
  CHECK(q3sound->channel == 2 && q3sound->volume == 1 && !q3sound->loop);
  CHECK(ev[26].q3_source->kind == QA_APP_Q3_EVENT_SERVER_COMMAND && ev[26].q3_source->client == 1);
  CHECK_STR(ev[26].q3_source->text, "cp \"Fight!\"");
}

/* The emit hook the events host calls: the sentinel ctx reaches each leaf. */
static int pres_emit_super_calls;
static qa_source_time pres_now(void *ctx) { CHECK_SENTINEL(ctx); return (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, 2500}; }
static bool pres_client_for(void *ctx, qa_actor_id actor, qa_client_id *out) { CHECK_SENTINEL(ctx); (void)actor; (void)out; return false; }
static bool pres_source_slot(void *ctx, qa_actor_id actor, double *out) { CHECK_SENTINEL(ctx); *out = (double)actor.slot; return true; }
static bool pres_resource_for(void *ctx, qa_content_id content, const char *path, qa_resource_id *out) {
  CHECK_SENTINEL(ctx); (void)content; (void)path; (void)out; return false;
}
static void pres_emit_super(void *ctx, bool has_owner, qa_presentation_owner owner, qa_content_id content,
    qa_app_sim_source_event_input source, qa_source_time time, bool has_recipient, qa_actor_id recipient,
    qa_app_sim_presentation_event *out) {
  CHECK_SENTINEL(ctx);
  CHECK(time.kind == QA_SOURCE_TIME_MILLISECONDS && time.value == 2500);
  *out = qa_app_sim_presentation_event_view(&source);
  out->has_owner = has_owner; out->owner = owner; out->content = content;
  out->has_recipient = has_recipient; out->recipient = recipient;
  out->sequence = pres_emit_super_calls++;
  out->seconds = time.value / 1000;
}

static void pres_events_tests(void) {
  static int token;
  qa_app_sim_presentation_event built[32];
  size_t count = pres_build_events(built, &token);
  CHECK(count == 27);
  pres_store store; pres_store_init(&store);
  for (size_t i = 0; i < count; i++) CHECK(pres_store_receive(&store, &built[i]));
  /* The producers' storage is gone: owned values must not reach it. */
  pres_scribble_producers();
  pres_check_events(store.events, &token);
  /* Kinds are the canonical literals, not the producer's (freed) copies. */
  CHECK(store.events[0].kind != NULL && strcmp(store.events[0].kind, "presentation-owner") == 0);

  /* Copies of owned values are independent of the value they came from. */
  qa_app_sim_presentation_event copies[27];
  for (size_t i = 0; i < 27; i++) CHECK(qa_app_sim_presentation_event_copy(&copies[i], &store.events[i]));
  pres_store_destroy(&store);
  pres_check_events(copies, &token);

  /* Source/view round trip shares the payload. */
  qa_app_sim_source_presentation_event source = qa_app_sim_presentation_event_source(&copies[19]);
  CHECK_STR(source.kind, "q2-composition");
  CHECK(source.q2_composition == copies[19].q2_composition);
  qa_app_sim_presentation_event view = qa_app_sim_presentation_event_view(&source);
  CHECK(view.storage == NULL && view.q2_composition == copies[19].q2_composition && view.sequence == 0);
  qa_app_sim_presentation_event_release(&view); /* a no-op on views */
  CHECK(view.q2_composition == copies[19].q2_composition);
  for (size_t i = 0; i < 27; i++) qa_app_sim_presentation_event_release(&copies[i]);
  CHECK(copies[3].storage == NULL && copies[3].kind == NULL);

  /* A full PresentationState ring: 32,768 owned events, then one more is refused. */
  pres_store full; pres_store_init(&full);
  qa_app_sim_presentation_event more[32];
  size_t more_count = pres_build_events(more, &token);
  for (size_t i = 0; i < PRES_EVENT_CAPACITY; i++) {
    if (!pres_store_receive(&full, &more[i % more_count])) { CHECK(false); break; }
  }
  CHECK(full.count == PRES_EVENT_CAPACITY);
  CHECK(!pres_store_receive(&full, &more[0]));
  pres_scribble_producers();
  CHECK(full.events[PRES_EVENT_CAPACITY - 1].sequence == PRES_EVENT_CAPACITY - 1);
  /* 32767 % 27 == 16: the q2-weapon rail beam. */
  CHECK_STR(full.events[PRES_EVENT_CAPACITY - 1].kind, "q2-weapon");
  if (strcmp(full.events[PRES_EVENT_CAPACITY - 1].kind, "q2-weapon") == 0)
    CHECK_STR(full.events[PRES_EVENT_CAPACITY - 1].q2_weapon->effect, "rail");
  pres_store_destroy(&full);

  /* SimulationEvents.emitOwned takes the typed source event: the q2-weapon
   * muzzleflash becomes a q2-muzzle-flash network message for its actor, and
   * the returned presentation is the super's view of the same payload. */
  qa_app_sim_simulation_events_options options; memset(&options, 0, sizeof options);
  options.ctx = PRES_SENTINEL;
  options.now = pres_now; options.client_for = pres_client_for; options.source_slot = pres_source_slot;
  options.resource_for = pres_resource_for; options.emit_super = pres_emit_super;
  qa_app_sim_simulation_events *events = qa_app_sim_simulation_events_create(options);
  qa_actor_id shooter = pres_actor(9, 1, &token);
  qa_q2w_event flash; memset(&flash, 0, sizeof flash);
  flash.kind = QA_Q2W_EVENT_MUZZLEFLASH; flash.actor = &shooter; flash.flash = 6;
  qa_app_sim_source_presentation_event weapon; memset(&weapon, 0, sizeof weapon);
  weapon.kind = "q2-weapon"; weapon.q2_weapon = &flash;
  qa_presentation_owner no_owner; memset(&no_owner, 0, sizeof no_owner);
  qa_actor_id nobody; memset(&nobody, 0, sizeof nobody);
  qa_app_sim_presentation_event emitted = qa_app_sim_simulation_events_emit_owned(events, false, no_owner,
      "q2:baseq2:base:1", weapon, false, (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, 0}, false, nobody);
  CHECK_STR(emitted.kind, "q2-weapon");
  CHECK(emitted.q2_weapon == &flash && emitted.seconds == 2.5);
  size_t taken = 0;
  const qa_simulation_event *out = qa_app_sim_simulation_events_take(events, &taken);
  CHECK(taken == 1);
  if (taken == 1) {
    CHECK(out[0].payload.kind == QA_SIM_EVENT_MESSAGE && out[0].payload.event.kind == QA_NETWORK_EVENT_Q2_MUZZLE_FLASH);
    CHECK(out[0].payload.event.entity_number == 9 && out[0].payload.event.flash == 6 && !out[0].payload.event.monster);
    CHECK(strcmp(out[0].audience.kind, "world") == 0);
  }
  /* A q2-composition ctf scoreboard publishes its layout to the event actor. */
  qa_q2_ctf_event board; memset(&board, 0, sizeof board);
  board.kind = "scoreboard"; board.actor = shooter; board.layout = "xv 0 string \"x\"";
  qa_q2_composition_event composition; memset(&composition, 0, sizeof composition);
  composition.kind = QA_Q2_COMPOSITION_EVENT_CTF; composition.ctf = &board;
  qa_app_sim_source_presentation_event scoreboard; memset(&scoreboard, 0, sizeof scoreboard);
  scoreboard.kind = "q2-composition"; scoreboard.q2_composition = &composition;
  qa_app_sim_simulation_events_emit(events, "q2:baseq2:base:1", scoreboard, false,
      (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, 0}, false, nobody);
  /* client_for answers false: TS drops output for an actor with no client. */
  out = qa_app_sim_simulation_events_take(events, &taken);
  CHECK(taken == 0);
  CHECK(pres_emit_super_calls == 2);
  qa_app_sim_simulation_events_destroy(events);
  pres_scribble_producers();
}
