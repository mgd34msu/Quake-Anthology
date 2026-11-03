/*
 * C checks for src/content/q1/addons rows (context, brushes, commands,
 * effects, field-triggers, horde, rope, triggers). Expected values come
 * from running the TypeScript addon code under bun (random fixed at 0.4).
 * The addons run over the real Q1 game and world tables
 * (q1_addon_fixture.h); every addon host callback checks a sentinel ctx.
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "q1_addon_fixture.h"
#include "qa/persistence/value.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static qa_q1_actor *make(q1_addon_fixture *af, const char *classname) { return q1af_create(af, classname); }
static void set_text(qa_q1_actor *entity, const char *key, const char *value) { CHECK(qa_q1_actor_field_set(entity, key, value)); }

/* Q1HordeServices: every member checks the sentinel ctx. */
static double hs_dead_flag(void *ctx, qa_actor_id player) { (void)player; Q1AF_CTX(ctx); return 0; }
static bool hs_no_target(void *ctx, qa_actor_id player) { (void)player; Q1AF_CTX(ctx); return false; }
static bool hs_is_bot(void *ctx, qa_actor_id player) { (void)player; Q1AF_CTX(ctx); return false; }
static void hs_respawn_teammate(void *ctx, qa_actor_id player) { (void)player; Q1AF_CTX(ctx); }
static void hs_add_score(void *ctx, qa_actor_id player, double delta) { (void)player; (void)delta; Q1AF_CTX(ctx); }
static void hs_restart_session(void *ctx, const char *map, double flags) { (void)map; (void)flags; Q1AF_CTX(ctx); }

static void test_context(void) {
  q1_addon_fixture af;
  q1af_open(&af, QA_Q1AD_MG1);
  CHECK(af.context != NULL);
  qa_q1_actor *e = make(&af, "x");
  qa_q1ad_set_number(af.context, e, "n", 0.1);
  qa_q1ad_set_vector(af.context, e, "v", (qa_vec3){1.0 / 3, -2.5, 1e-7});
  CHECK(strcmp(qa_q1_actor_text(e, "n"), "0.10000000149011612") == 0);
  CHECK(strcmp(qa_q1_actor_text(e, "v"), "0.3333333432674408 -2.5 1.0000000116860974e-7") == 0);

  /* Release keeps capture order: TS players [b,c] with words [[b1],[c1]]. */
  qa_owned_actor a = q1_world_fixture_admit_player(&af.fx, QA_Q1_ZERO, 100);
  qa_owned_actor b = q1_world_fixture_admit_player(&af.fx, QA_Q1_ZERO, 100);
  qa_owned_actor c = q1_world_fixture_admit_player(&af.fx, QA_Q1_ZERO, 100);
  qa_owned_actor d = q1_world_fixture_admit_player(&af.fx, QA_Q1_ZERO, 100);
  qa_q1ad_set_player_number(af.context, a.id, "a1", 1);
  qa_q1ad_set_player_number(af.context, b.id, "b1", 2);
  qa_q1ad_set_player_number(af.context, a.id, "a2", 3);
  qa_q1ad_set_player_number(af.context, c.id, "c1", 4);
  qa_session_actor_registry_release(af.fx.actors, a);
  const qa_q1_state_extension *extension = q1af_extension(&af);
  CHECK(extension != NULL);
  uint8_t *bytes = NULL;
  size_t length = 0;
  CHECK(extension->capture(extension->ctx, &bytes, &length));
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  qa_save_reader root, players, first, second, slot1, slot2, words1, word1, key1;
  CHECK(qa_save_reader_init(&root, decoded, "root") && qa_save_reader_field(&root, "players", &players));
  CHECK(qa_save_reader_list_count(&players) == 2);
  CHECK(qa_save_reader_item(&players, 0, &first) && qa_save_reader_item(&players, 1, &second));
  CHECK(qa_save_reader_field(&first, "slot", &slot1) && qa_save_reader_field(&second, "slot", &slot2));
  CHECK(qa_save_reader_number(&slot1) == (double)b.id.slot && qa_save_reader_number(&slot2) == (double)c.id.slot);
  CHECK(qa_save_reader_field(&first, "words", &words1) && qa_save_reader_item(&words1, 0, &word1) &&
        qa_save_reader_item(&word1, 0, &key1));
  CHECK(strcmp(qa_save_reader_string(&key1), "b1") == 0);
  qa_save_reader *all[] = {&key1, &word1, &words1, &slot2, &slot1, &second, &first, &players, &root};
  for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) qa_save_reader_destroy(all[i]);
  qa_save_value_free(decoded);
  free(bytes);

  /* Restore stores value.number() unrounded: TS playerNumber 0.1. */
  qa_save_value *rootv = qa_save_value_object(), *pl = qa_save_value_array(), *p0 = qa_save_value_object();
  qa_save_value *ws = qa_save_value_array(), *pair = qa_save_value_array();
  CHECK(qa_save_value_push(pair, qa_save_value_string("w")) && qa_save_value_push(pair, qa_save_value_number(0.1)));
  CHECK(qa_save_value_push(ws, pair) && qa_save_value_set(p0, "slot", qa_save_value_number((double)d.id.slot)) &&
        qa_save_value_set(p0, "generation", qa_save_value_number((double)d.id.generation)) && qa_save_value_set(p0, "words", ws));
  CHECK(qa_save_value_push(pl, p0) && qa_save_value_set(rootv, "players", pl) &&
        qa_save_value_set(rootv, "references", qa_save_value_array()) && qa_save_value_set(rootv, "ticks", qa_save_value_array()));
  bytes = qa_encode_checkpoint_value(rootv, &length);
  qa_save_value_free(rootv);
  extension->restore(extension->ctx, bytes, length);
  free(bytes);
  CHECK(qa_q1ad_player_number(af.context, d.id, "w") == 0.1);
  q1af_close(&af);
}

static void test_brushes(void) {
  q1_addon_fixture af;
  q1af_open(&af, QA_Q1AD_MG1);
  CHECK(af.context != NULL);
  qa_q1ad_register_brushes(af.context);
  qa_q1_actor *e = make(&af, "func_shatter");
  q1af_set_body(&af, e, (qa_vec3){3, -7, 11}, (qa_bounds){{-13, -5, -9}, {17, 29, 7}});
  set_text(e, "pos2", "41 -2 -97");
  CHECK(q1af_has_spawner(&af, "func_shatter"));
  qa_q1_spawn_entity(af.fx.game, e);
  CHECK(e->speed == 156.32327270507813);
  CHECK(e->movedir.x == -51.753421783447266 && e->movedir.y == 7.6742773056030273 && e->movedir.z == 145.87821960449219);

  /* toss_cascade: TS Math.min keeps NaN, so the delay stays 7. */
  qa_q1_actor *ta = make(&af, "func_toss"), *tb = make(&af, "func_toss"), *m = make(&af, "func_toss");
  set_text(ta, "netname", "_toss_origin");
  set_text(tb, "netname", "_toss_origin");
  qa_q1ad_set_text(&ta->targetname, "t");
  qa_q1ad_set_text(&tb->targetname, "t");
  qa_q1ad_set_text(&m->targetname, "t");
  ta->speed = 0;
  tb->speed = 4;
  m->delay = 7;
  q1af_set_body(&af, tb, QA_Q1_ZERO, (qa_bounds){{10, 0, 0}, {10, 0, 0}});
  q1af_action(&af, m, "mg1:brush:toss_cascade");
  CHECK(m->delay == 7);
  q1af_close(&af);
}

static void test_effects_and_push(void) {
  q1_addon_fixture af;
  q1af_open(&af, QA_Q1AD_MG1);
  CHECK(af.context != NULL);
  qa_q1ad_register_effects(af.context);
  qa_q1_actor *e = make(&af, "particle_tele");
  q1af_set_body(&af, e, (qa_vec3){5, 6, 7}, QA_Q1_POINT);
  af.fx.random_value = 0.15;
  q1af_action(&af, e, "mg1:fx:tele");
  af.fx.random_value = 0.4;
  const qa_q1_event *particles = q1af_first_event(&af, "particles");
  CHECK(particles != NULL);
  if (particles != NULL) {
    CHECK(particles->origin.x == -37.666664123535156 && particles->origin.y == -36.666664123535156 &&
          particles->origin.z == -14.333332061767578);
    CHECK(particles->direction.x == 5.3333330154418945 && particles->direction.y == 5.3333330154418945 &&
          particles->direction.z == 2.6666665077209473);
  }
  q1af_close(&af);

  q1af_open(&af, QA_Q1AD_MG3);
  CHECK(af.context != NULL);
  qa_q1ad_register_field_triggers(af.context);
  qa_q1_actor *push = make(&af, "trigger_push");
  set_text(push, "movedir", "3 4 12.5");
  CHECK(q1af_has_spawner(&af, "trigger_push"));
  qa_q1_spawn_entity(af.fx.game, push);
  CHECK(strcmp(qa_q1_actor_text(push, "movedir"), "0.22283440828323364 0.2971125543117523 0.9284766912460327") == 0);
  q1af_close(&af);
}

static void test_commands_marker(void) {
  q1_addon_fixture af;
  q1af_open(&af, QA_Q1AD_MG3);
  CHECK(af.context != NULL);
  qa_q1_actor *t = make(&af, "trigger_secret");
  q1af_set_body(&af, t, (qa_vec3){100, 37, -20}, (qa_bounds){{-8, -8, -8}, {8, 8, 8}});
  qa_owned_actor player = q1_world_fixture_admit_player(&af.fx, (qa_vec3){1, 2, 3}, 100);
  qa_q1ad_set_player_number(af.context, player.id, "secrethunter", 1);
  qa_q1ad_frame_player(af.context, player.id, (qa_vec3){0, 0, 22});
  qa_q1_actor *marker = NULL;
  for (size_t i = 0; i < af.fx.game->entity_count; i++)
    if (strcmp(af.fx.game->entities[i]->classname, "secret_marker") == 0) marker = af.fx.game->entities[i];
  CHECK(marker != NULL);
  if (marker != NULL) {
    qa_vec3 origin = qa_q1_get_body(af.fx.game, marker).origin;
    CHECK(origin.x == 96.533645629882813 && origin.y == 35.774520874023438 && origin.z == -18.424383163452148);
  }
  q1af_close(&af);
}

static void test_rope(void) {
  q1_addon_fixture af;
  q1af_open(&af, QA_Q1AD_MG3);
  CHECK(af.context != NULL);
  qa_q1ad_register_ropes(af.context);
  qa_q1_actor *rope = make(&af, "misc_rope");
  qa_actor_id stale = rope->actor.id; /* an id no live actor holds */
  stale.slot = 77;
  stale.generation = 3;
  CHECK(qa_q1_actor_reference_set(rope, "rope.chain", &stale));
  set_text(rope, "oldorigin", "0 0 0");
  q1af_action(&af, rope, "mg3:rope:tick");
  size_t segments = 0;
  qa_q1_actor *first = NULL;
  for (size_t i = 0; i < af.fx.game->entity_count; i++)
    if (strcmp(af.fx.game->entities[i]->classname, "misc_rope_segment") == 0) {
      if (first == NULL) first = af.fx.game->entities[i];
      segments++;
    }
  CHECK(segments == 32);
  qa_actor_id chain = {.slot = 0, .generation = 0};
  CHECK(first != NULL && qa_q1_actor_reference_get(first, "rope.chain", &chain) && chain.slot == 77 && chain.generation == 3);
  q1af_close(&af);
}

static void test_triggers(void) {
  q1_addon_fixture af;
  q1af_open(&af, QA_Q1AD_MG3);
  CHECK(af.context != NULL);
  qa_q1ad_register_triggers(af.context);
  qa_owned_actor player = q1_world_fixture_admit_player(&af.fx, QA_Q1_ZERO, 100);
  qa_q1_actor *e = make(&af, "trigger_sacrifice_counter");
  e->count = 3.5;
  qa_q1_call_use(af.fx.game, e, qa_q1_callback_registry_use(af.fx.game->named, e, "mg3:trigger:sacrifice_counter"), NULL,
                 &player.id);
  const qa_q1_event *message = q1af_first_event(&af, "message");
  CHECK(q1af_count_events(&af, "message") == 1 && message != NULL && strcmp(message->text, "$mg3_qc_sacricie_count_2.5_more") == 0);
  q1af_close(&af);

  /* change_target sees every entity: TS rewrites 601 targets. */
  q1af_open(&af, QA_Q1AD_MG1);
  CHECK(af.context != NULL);
  qa_q1ad_register_triggers(af.context);
  for (int i = 0; i < 600; i++) qa_q1ad_set_text(&make(&af, "f")->target, "old");
  qa_q1_actor *changer = make(&af, "trigger_changetarget");
  qa_q1ad_set_text(&changer->target, "old");
  qa_q1ad_set_text(&changer->killtarget, "new");
  qa_q1_call_use(af.fx.game, changer, qa_q1_callback_registry_use(af.fx.game->named, changer, "mg1:trigger:change_target"), NULL,
                 NULL);
  size_t changed = 0;
  for (size_t i = 0; i < af.fx.game->entity_count; i++)
    if (strcmp(af.fx.game->entities[i]->target, "new") == 0) changed++;
  CHECK(changed == 601);
  q1af_close(&af);
}

static qa_q1ad_horde_services horde_services(void) {
  qa_q1ad_horde_services services = {.ctx = &q1af_sentinel, .dead_flag = hs_dead_flag, .no_target = hs_no_target,
                                     .is_bot = hs_is_bot, .respawn_teammate = hs_respawn_teammate,
                                     .add_score = hs_add_score, .restart_session = hs_restart_session};
  return services;
}

static void test_horde_sound(void) {
  q1_addon_fixture af;
  q1af_open(&af, QA_Q1AD_MG1);
  CHECK(af.context != NULL);
  qa_q1ad_horde *horde = qa_q1ad_register_horde(af.context, horde_services());
  CHECK(horde != NULL);
  qa_q1_actor *manager = make(&af, "horde_manager");
  q1af_action(&af, manager, "mg1:horde:countdown2");
  const qa_q1_event *sound = q1af_first_event(&af, "sound");
  CHECK(q1af_count_events(&af, "sound") == 1 && sound != NULL && strcmp(sound->path, "misc/talk.wav") == 0 &&
        strcmp(sound->sound_channel, "voice") == 0 && sound->attenuation == 0);
  qa_q1ad_horde_destroy(horde);
  q1af_close(&af);
}

static void test_no_keys(void) {
  q1_addon_fixture af;
  q1af_open(&af, QA_Q1AD_MG1);
  CHECK(af.context != NULL);
  qa_owned_actor player = q1_world_fixture_admit_player(&af.fx, QA_Q1_ZERO, 100);
  CHECK(qa_q1_attach_player(af.fx.game, player, "axe", false, NULL) != NULL);
  char names[40][16];
  for (size_t i = 0; i < 40; i++) {
    snprintf(names[i], sizeof names[i], "x:%zu", i);
    qa_inventory_entry entry = {.item = i == 35 ? "q1:ammo/rockets" : names[i], .count = 1, .capacity = i == 35 ? 77 : 1};
    qa_shared_inventory_table_configure(af.fx.inventory, player, entry, NULL, NULL);
  }
  CHECK(qa_q1ad_handle_impulse(af.context, player.id, 99));
  /* TS: {"item":"q1:ammo/rockets","count":100,"capacity":77} */
  size_t count = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(af.fx.inventory, player.id, &count);
  double rockets_count = -1, rockets_capacity = -1;
  for (size_t i = 0; i < count; i++)
    if (strcmp(entries[i].item, "q1:ammo/rockets") == 0) { rockets_count = entries[i].count; rockets_capacity = entries[i].capacity; }
  CHECK(rockets_count == 100 && rockets_capacity == 77);
  q1af_close(&af);
}

static void test_horde_spawn_monster(void) {
  q1_addon_fixture af;
  q1af_open(&af, QA_Q1AD_MG1);
  CHECK(af.context != NULL);
  qa_q1ad_horde *horde = qa_q1ad_register_horde(af.context, horde_services());
  qa_q1_actor *owner = make(&af, "horde_manager");
  qa_q1_actor *m = qa_q1ad_horde_spawn_monster(horde, "ogre", QA_Q1_ZERO, QA_Q1_ZERO, owner);
  /* TS over the real game, base and a bare mg1 context (bun, Q1AddonContext + registerQ1Horde):
   * model "progs/ogre.mdl", fields [["source.monsterCallbackPrefix","base"],["killstring","$qc_ks_ogre"],
   * ["view_ofs","0 0 25"],["category","monster"],["horde.sourceDie","base:monster_die"]]. The former
   * mirror fixture faked the base monster spawn and so lacked the two base fields. */
  CHECK(m != NULL && strcmp(m->model, "progs/ogre.mdl") == 0);
  CHECK(m != NULL && m->field_count == 5 && strcmp(m->fields[0].key, "source.monsterCallbackPrefix") == 0 &&
        strcmp(m->fields[1].key, "killstring") == 0 && strcmp(m->fields[2].key, "view_ofs") == 0 &&
        strcmp(m->fields[3].key, "category") == 0 && strcmp(m->fields[4].key, "horde.sourceDie") == 0);
  CHECK(m != NULL && strcmp(qa_q1_actor_text(m, "horde.sourceDie"), "base:monster_die") == 0 &&
        strcmp(qa_q1_actor_text(m, "view_ofs"), "0 0 25") == 0 && strcmp(qa_q1_actor_text(m, "category"), "monster") == 0);
  qa_q1ad_horde_destroy(horde);
  q1af_close(&af);
}

static void push_player(qa_save_value *list, qa_actor_id actor, qa_save_value *words) {
  qa_save_value *p = qa_save_value_object();
  CHECK(qa_save_value_set(p, "slot", qa_save_value_number((double)actor.slot)) &&
        qa_save_value_set(p, "generation", qa_save_value_number((double)actor.generation)) &&
        qa_save_value_set(p, "words", words) && qa_save_value_push(list, p));
}
/* Capture as "slot:word,word;" for comparison with the TS spelling. */
static void capture_text(q1_addon_fixture *af, char *out, size_t size) {
  const qa_q1_state_extension *extension = q1af_extension(af);
  uint8_t *bytes = NULL;
  size_t length = 0;
  CHECK(extension != NULL && extension->capture(extension->ctx, &bytes, &length));
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  qa_save_reader root, players;
  CHECK(qa_save_reader_init(&root, decoded, "root") && qa_save_reader_field(&root, "players", &players));
  out[0] = '\0';
  for (size_t i = 0; i < qa_save_reader_list_count(&players); i++) {
    qa_save_reader player, slot, words;
    CHECK(qa_save_reader_item(&players, i, &player) && qa_save_reader_field(&player, "slot", &slot) &&
          qa_save_reader_field(&player, "words", &words));
    size_t used = strlen(out);
    snprintf(out + used, size - used, "%g:", qa_save_reader_number(&slot));
    for (size_t j = 0; j < qa_save_reader_list_count(&words); j++) {
      qa_save_reader pair, key, value;
      CHECK(qa_save_reader_item(&words, j, &pair) && qa_save_reader_item(&pair, 0, &key) && qa_save_reader_item(&pair, 1, &value));
      used = strlen(out);
      snprintf(out + used, size - used, "%s=%g,", qa_save_reader_string(&key), qa_save_reader_number(&value));
      qa_save_reader_destroy(&value); qa_save_reader_destroy(&key); qa_save_reader_destroy(&pair);
    }
    used = strlen(out);
    snprintf(out + used, size - used, ";");
    qa_save_reader_destroy(&words); qa_save_reader_destroy(&slot); qa_save_reader_destroy(&player);
  }
  qa_save_reader_destroy(&players); qa_save_reader_destroy(&root);
  qa_save_value_free(decoded);
  free(bytes);
}
static void test_restore_empty_player(void) {
  q1_addon_fixture af;
  q1af_open(&af, QA_Q1AD_MG1);
  CHECK(af.context != NULL);
  qa_owned_actor five = q1_world_fixture_admit_player(&af.fx, QA_Q1_ZERO, 100);
  qa_owned_actor six = q1_world_fixture_admit_player(&af.fx, QA_Q1_ZERO, 100);
  qa_save_value *root = qa_save_value_object(), *players = qa_save_value_array();
  qa_save_value *words6 = qa_save_value_array(), *pair = qa_save_value_array();
  CHECK(qa_save_value_push(pair, qa_save_value_string("w")) && qa_save_value_push(pair, qa_save_value_number(2)) &&
        qa_save_value_push(words6, pair));
  push_player(players, five.id, qa_save_value_array());
  push_player(players, six.id, words6);
  CHECK(qa_save_value_set(root, "players", players) && qa_save_value_set(root, "references", qa_save_value_array()) &&
        qa_save_value_set(root, "ticks", qa_save_value_array()));
  size_t length = 0;
  uint8_t *bytes = qa_encode_checkpoint_value(root, &length);
  qa_save_value_free(root);
  const qa_q1_state_extension *extension = q1af_extension(&af);
  extension->restore(extension->ctx, bytes, length);
  free(bytes);
  /* TS (players 5 and 6): [[5,[]],[6,[["w",2]]]] */
  char text[256], expected[256];
  capture_text(&af, text, sizeof text);
  snprintf(expected, sizeof expected, "%g:;%g:w=2,;", (double)five.id.slot, (double)six.id.slot);
  CHECK(strcmp(text, expected) == 0);
  /* TS after setPlayerNumber(5, "a", 1): [[5,[["a",1]]],[6,[["w",2]]]] */
  qa_q1ad_set_player_number(af.context, five.id, "a", 1);
  capture_text(&af, text, sizeof text);
  snprintf(expected, sizeof expected, "%g:a=1,;%g:w=2,;", (double)five.id.slot, (double)six.id.slot);
  CHECK(strcmp(text, expected) == 0);
  CHECK(qa_q1ad_player_number(af.context, five.id, "a") == 1);
  qa_session_actor_registry_release(af.fx.actors, five);
  capture_text(&af, text, sizeof text);
  snprintf(expected, sizeof expected, "%g:w=2,;", (double)six.id.slot);
  CHECK(strcmp(text, expected) == 0);
  q1af_close(&af);
}

int main(void) {
  test_context();
  test_brushes();
  test_effects_and_push();
  test_commands_marker();
  test_rope();
  test_triggers();
  test_horde_sound();
  test_no_keys();
  test_horde_spawn_monster();
  test_restore_empty_player();
  CHECK(q1af_ctx_errors == 0); /* every Q1AddonServices / campaign / horde callback saw the sentinel */
  checks += q1af_ctx_checks;    /* each callback invocation's sentinel check */
  failures += q1af_ctx_errors;
  printf("test_q1_rows_3: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
