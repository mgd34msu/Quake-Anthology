/*
 * Port of key cases for src/content/q2/foundation + equipment.
 * No dedicated TS test files exist under tests/content/q2 for these
 * modules (only lmctf application/console tests); cases are derived
 * from the TS source behavior. GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/content/composition/q2_q3_items.h"
#include "qa/content/q2/equipment/grapple_services.h"
#include "qa/content/q2/equipment/grapple_weapon.h"
#include "qa/content/q2/foundation.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

#define CHECK_ABORTS(...)                                               \
  do {                                                                  \
    checks++;                                                           \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
      freopen("/dev/null", "w", stderr);                                \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {        \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,     \
              __LINE__, #__VA_ARGS__);                                  \
    }                                                                   \
  } while (0)

static bool near(double a, double b) { return fabs(a - b) < 1e-9; }

static void test_fields(void) {
  qa_q2_spawn_fields *entities = NULL;
  size_t count = 0;
  const char *src = "{\n\"classname\" \"worldspawn\"\n\"message\" \"a\\nb\"\n}\n"
                    "// comment\n{ \"classname\" \"func_door\" \"angle\" \"90\" \"origin\" \"1 2 3\" \"spawnflags\" \"256\" }";
  CHECK(qa_q2_parse_entities(src, QA_Q2_EDITION_CLASSIC, &entities, &count));
  CHECK(count == 2);
  CHECK(strcmp(entities[0].classname, "worldspawn") == 0);
  const char *msg = qa_q2_spawn_get(&entities[0], "message");
  CHECK(msg && strcmp(msg, "a\nb") == 0);
  CHECK(near(qa_q2_number_field(&entities[1], "angle", 0), 90));
  CHECK(near(qa_q2_number_field(&entities[1], "missing", 7), 7));
  CHECK(near(qa_q2_integer_field(&entities[1], "spawnflags", 0), 256));
  qa_vec3 origin = qa_q2_vector_field(&entities[1], "origin");
  CHECK(origin.x == 1 && origin.y == 2 && origin.z == 3);
  qa_vec3 none = qa_q2_vector_field(&entities[1], "nope");
  CHECK(none.x == 0 && none.y == 0 && none.z == 0);
  qa_q2_game_options o = {0};
  o.mode = QA_Q2_MODE_SINGLEPLAYER; o.skill = 0;
  CHECK(qa_q2_inhibit_spawn(&entities[1], o) == true);
  o.skill = 2;
  CHECK(qa_q2_inhibit_spawn(&entities[1], o) == false);
  CHECK(qa_q2_inhibit_spawn(&entities[0], o) == false);
  qa_q2_entities_free(entities, count);

  qa_vec3 up = qa_q2_movedir((qa_vec3){0, -1, 0});
  CHECK(up.x == 0 && up.y == 0 && up.z == 1);
  qa_vec3 down = qa_q2_movedir((qa_vec3){0, -2, 0});
  CHECK(down.z == -1);
  qa_vec3 fwd = qa_q2_movedir((qa_vec3){0, 0, 0});
  CHECK(near(fwd.x, 1) && near(fwd.y, 0) && near(fwd.z, 0));

  CHECK_ABORTS((void)qa_q2_parse_entities("nope", QA_Q2_EDITION_CLASSIC, &entities, &count));
  CHECK_ABORTS((void)qa_q2_parse_entities("{ \"classname\"", QA_Q2_EDITION_CLASSIC, &entities, &count));
  CHECK_ABORTS((void)qa_q2_parse_entities("{ \"classname\" }", QA_Q2_EDITION_CLASSIC, &entities, &count));
}

static void test_start_items(void) {
  size_t count = 0;
  qa_q2_start_item *items = qa_q2_parse_start_items("weapon_shotgun; ammo_shells 25 ; ;item_quad", &count);
  CHECK(count == 3);
  CHECK(strcmp(items[0].classname, "weapon_shotgun") == 0 && items[0].count == 1);
  CHECK(strcmp(items[1].classname, "ammo_shells") == 0 && items[1].count == 25);
  CHECK(strcmp(items[2].classname, "item_quad") == 0);
  qa_q2_start_items_free(items, count);
  items = qa_q2_parse_start_items("", &count);
  CHECK(count == 0);
  qa_q2_start_items_free(items, count);
  CHECK_ABORTS((void)qa_q2_parse_start_items("bad-item!", &count));
  CHECK_ABORTS((void)qa_q2_parse_start_items("ammo_shells x", &count));
}

static void test_effect_resources_and_attachments(void) {
  CHECK(strcmp(QA_Q2_TRANSIENT_MODELS.cable, "models/ctf/segment/tris.md2") == 0);
  CHECK(strcmp(QA_Q2_TRANSIENT_MODELS.bfg_explosion, "sprites/s_bfg2.sp2") == 0);
  CHECK(QA_Q2_TRANSIENT_SOUNDS_COUNT == 14);
  CHECK(strcmp(QA_Q2_TRANSIENT_SOUNDS[0], "world/ric1.wav") == 0);
  CHECK(strcmp(QA_Q2_TRANSIENT_SOUNDS[13], "player/fall1.wav") == 0);
  CHECK(QA_Q2_ROGUE_TRANSIENT_SOUNDS_COUNT == 2);
  const qa_model_attachment_definition *def =
      qa_q2_weapon_attachment("sha256:32e4effc4d859237a6b0fbd5ebd2c0854f4705fb3baa577dc63f7f47d1e60e44");
  CHECK(def != NULL);
  CHECK(def && !def->is_joint && def->vertices[0] == 0 && def->vertices[1] == 23 && def->vertices[2] == 24);
  CHECK(def && near(def->grip.origin.x, -2.5316379070281982));
  const qa_model_attachment_definition *joint =
      qa_q2_weapon_attachment("sha256:a66ba6a201c31a82b2b9e443123bba88968cedd9dc774182d8c22659aa0044dc");
  CHECK(joint && joint->is_joint && strcmp(joint->name, "Weapon") == 0);
  CHECK(qa_q2_weapon_attachment("sha256:0000000000000000000000000000000000000000000000000000000000000000") == NULL);
}

static void test_held_weapons(void) {
  qa_held_weapon_model out;
  char buf[256];
  CHECK(qa_q2_held_weapon("models/weapons/v_blast/tris.md2", NULL, &out, buf, sizeof buf));
  CHECK(strcmp(out.path, "players/male/w_blaster.md2") == 0);
  CHECK(strcmp(out.fallback, "players/male/weapon.md2") == 0);
  CHECK(qa_q2_held_weapon("", "q2:weapon_railgun", &out, buf, sizeof buf));
  CHECK(strcmp(out.path, "players/male/w_railgun.md2") == 0);
  CHECK(qa_q2_held_weapon("models/weapons/grapple/tris.md2", NULL, &out, buf, sizeof buf));
  CHECK(strcmp(out.path, "players/male/w_grapple.md2") == 0);
  CHECK(!qa_q2_held_weapon("models/nope.md2", NULL, &out, buf, sizeof buf));
  CHECK(!qa_q2_held_weapon("", "q2:weapon_nope", &out, buf, sizeof buf));
}

static void think_noop(void *c, qa_q2_entity *e, qa_q2_game_services *g) { (void)c; (void)e; (void)g; }
static void think_noop2(void *c, qa_q2_entity *e, qa_q2_game_services *g) { (void)c; (void)e; (void)g; }

static void test_callbacks(void) {
  qa_q2_source_callbacks *cbs = qa_q2_source_callbacks_create();
  CHECK(cbs != NULL);
  qa_q2_named_think thinks[] = {{"my_think", {NULL, think_noop}}};
  qa_q2_callback_definitions defs = {0};
  defs.think = thinks; defs.think_count = 1;
  qa_q2_source_callbacks_register(cbs, defs);
  qa_q2_source_callbacks_register(cbs, defs); /* same fn: ok */
  const char *name = qa_q2_think_name(cbs, (qa_q2_think){NULL, think_noop}, true);
  CHECK(name && strcmp(name, "my_think") == 0);
  CHECK(qa_q2_think_name(cbs, (qa_q2_think){0}, false) == NULL);
  bool has = false;
  qa_q2_think got = qa_q2_think_resolve(cbs, "my_think", &has);
  CHECK(has && got.fn == think_noop);
  got = qa_q2_think_resolve(cbs, NULL, &has);
  CHECK(!has && got.fn == NULL);
  qa_q2_named_think dup[] = {{"my_think", {NULL, think_noop2}}};
  qa_q2_callback_definitions dup_defs = {0};
  dup_defs.think = dup; dup_defs.think_count = 1;
  CHECK_ABORTS(qa_q2_source_callbacks_register(cbs, dup_defs));
  CHECK_ABORTS((void)qa_q2_think_name(cbs, (qa_q2_think){NULL, think_noop2}, true));
  CHECK_ABORTS((void)qa_q2_think_resolve(cbs, "missing", &has));
  qa_q2_source_callbacks_destroy(cbs);
}

static qa_actor_id ref_saved(void *c, qa_saved_actor_id saved) {
  (void)c;
  qa_actor_id out = {0};
  out.slot = (int64_t)saved.slot;
  out.generation = (int64_t)saved.generation;
  return out;
}

static void test_checkpoint_attack(void) {
  qa_actor_id attacker = {0}, inflictor = {0};
  attacker.slot = 3; attacker.generation = 1;
  inflictor.slot = 4; inflictor.generation = 2;
  qa_attack_provenance attack = {0};
  attack.attacker = &attacker;
  attack.inflictor = &inflictor;
  attack.sequence = 7;
  qa_q2_attack_checkpoint saved = qa_q2_save_attack(attack);
  CHECK(saved.has_attacker && saved.attacker.slot == 3 && saved.attacker.generation == 1);
  CHECK(saved.has_inflictor && saved.inflictor.slot == 4);
  CHECK(!saved.has_originating_projectile);
  qa_actor_id a_scratch, i_scratch;
  qa_attack_provenance restored = qa_q2_restore_attack(saved, ref_saved, NULL, &a_scratch, &i_scratch);
  CHECK(restored.attacker && restored.attacker->slot == 3);
  CHECK(restored.inflictor && restored.inflictor->generation == 2);
  CHECK(restored.sequence == 7);
  qa_saved_actor_id out_saved;
  CHECK(!qa_q2_save_actor(NULL, &out_saved));
  CHECK(qa_q2_save_actor(&attacker, &out_saved) && out_saved.slot == 3);
}

static void hooks_noop_actor(void *c, qa_actor_id a, qa_item_id i, bool first) { (void)c; (void)a; (void)i; (void)first; }
static void hooks_silencer(void *c, qa_actor_id a, double charges) { (void)c; (void)a; (void)charges; }
static void hooks_power_armor(void *c, qa_actor_id a, const char *k) { (void)c; (void)a; (void)k; }

static void test_items_catalog(void) {
  CHECK(strcmp(qa_q2_item_pickup_name("ammo_shells"), "Shells") == 0);
  CHECK(strcmp(qa_q2_item_pickup_name("weapon_bfg"), "BFG10K") == 0);
  CHECK(qa_q2_item_pickup_name("nope") == NULL);
  size_t n = 0;
  const qa_q2_weapon_inventory_entry *winv = qa_q2_base_weapon_inventory(&n);
  CHECK(n == 16);
  CHECK(strcmp(winv[0].item, "q2:ammo_shells") == 0 && winv[0].capacity == 100);
  const qa_q2_base_item_icon *icons = qa_q2_base_item_icons(&n);
  CHECK(n == 17);
  CHECK(strcmp(icons[6].item, "q2:weapon_blaster") == 0 && strcmp(icons[6].icon, "w_blaster") == 0);
  CHECK(strcmp(qa_q2_base_weapon_display_name("q2:weapon_railgun"), "Railgun") == 0);
  CHECK(qa_q2_base_weapon_display_name("q2:nope") == NULL);

  qa_q2_item_hooks hooks = {0};
  hooks.weapon_picked = hooks_noop_actor;
  hooks.silencer = hooks_silencer;
  hooks.power_armor = hooks_power_armor;
  qa_q2_item_module *m = qa_q2_item_module_create(hooks);
  CHECK(m != NULL);
  const qa_q2_inventory_item *list = qa_q2_item_module_list(m, &n);
  CHECK(n >= 40);
  bool found_shard = false, found_foreign = false;
  for (size_t i = 0; i < n; i++) {
    if (strcmp(list[i].classname, "item_armor_shard") == 0) found_shard = true;
    if (strcmp(list[i].classname, "weapon_nailgun") == 0) found_foreign = true;
  }
  CHECK(found_shard);
  CHECK(!found_foreign); /* foreign Q3 rows excluded from list() */
  qa_q2_inventory_item found;
  CHECK(qa_q2_item_module_lookup(m, "Railgun", &found));
  CHECK(strcmp(found.id, "q2:weapon_railgun") == 0 && found.weapon);
  CHECK(qa_q2_item_module_lookup(m, "AMMO_SHELLS", &found));
  CHECK(found.quantity == 10 && found.capacity == 100);
  CHECK(!qa_q2_item_module_lookup(m, "nope", &found));
  CHECK(strcmp(qa_q2_item_module_item_name(m, "key_blue_key"), "Blue Key") == 0);
  /* TS foreignItems is supplied by composition: none without the hook */
  CHECK(qa_q2_item_module_item_name(m, "weapon_nailgun") == NULL);
  {
    qa_q2_item_hooks foreign_hooks = hooks;
    foreign_hooks.foreign_items = qa_composition_q3_foreign_item_definitions;
    qa_q2_item_module *with_foreign = qa_q2_item_module_create(foreign_hooks);
    /* foreign registration landed in the catalog; colliding names keep the Q2 rows */
    CHECK(strcmp(qa_q2_item_module_item_name(with_foreign, "weapon_nailgun"), "Nailgun") == 0);
    CHECK(strcmp(qa_q2_item_module_item_name(with_foreign, "weapon_railgun"), "Railgun") == 0);
    size_t foreign_n = 0;
    const qa_q2_inventory_item *foreign_list = qa_q2_item_module_list(with_foreign, &foreign_n);
    bool listed = false;
    for (size_t i = 0; i < foreign_n; i++)
      if (strcmp(foreign_list[i].classname, "weapon_nailgun") == 0) listed = true;
    CHECK(!listed && foreign_n == n);
    qa_q2_item_module_destroy(with_foreign);
  }
  size_t wn = 0;
  (void)qa_q2_item_module_weapon_inventory(m, &wn);
  CHECK(wn >= 16);
  qa_q2_item_definition dup = {0};
  dup.kind = "key"; dup.classname = "ammo_shells";
  CHECK_ABORTS(qa_q2_item_module_register(m, dup));
  qa_q2_item_module_destroy(m);
}

static void test_unrotate_and_grapple(void) {
  qa_vec3 v = qa_q2_unrotate_landmark((qa_vec3){1, 0, 0}, (qa_vec3){0, 0, 0});
  CHECK(near(v.x, 1) && near(v.y, 0) && near(v.z, 0));
  qa_vec3 yawed = qa_q2_unrotate_landmark((qa_vec3){1, 0, 0}, (qa_vec3){0, 90, 0});
  CHECK(near(yawed.x, 0) && near(yawed.y, -1) && near(yawed.z, 0));

  qa_ctf_grapple_state st;
  qa_ctf_grapple_state_init(&st);
  CHECK(!st.has_grapple && strcmp(st.grapple_state, "fly") == 0);
  st.has_grapple = true;
  st.grapple.slot = 9; st.grapple.generation = 2;
  st.grapple_state = "pull"; st.grapple_release_time = 12.5;
  qa_ctf_grapple_checkpoint cp = qa_capture_ctf_grapple(st);
  CHECK(cp.has_grapple && cp.grapple.slot == 9 && strcmp(cp.grapple_state, "pull") == 0);
  qa_lmctf_grapple_state lm;
  qa_lmctf_grapple_state_init(&lm);
  CHECK(lm.hook_state == 0 && !lm.hook_held);

  qa_grapple_weapon_state ws = qa_create_grapple_weapon_state();
  CHECK(ws.handoff == QA_Q2W_HANDOFF_HOLSTERED);
  CHECK(ws.animation.phase == QA_Q2W_ACTIVATING);
  ws.animation.phase = QA_Q2W_FIRING;
  qa_release_lmctf_grapple_weapon(&ws.animation);
  CHECK(ws.animation.phase == QA_Q2W_READY);
  CHECK(qa_ctf_grapple_weapon_should_reset(&ws.animation, true, false));
  CHECK(!qa_ctf_grapple_weapon_should_reset(&ws.animation, false, false));
  CHECK(strcmp(QA_Q2_CTF_GRAPPLE.name, "grapple") == 0 && QA_Q2_CTF_GRAPPLE.fire_last == 9);
  CHECK(QA_Q2_RERELEASE_CTF_GRAPPLE.fire_last == 10);
  CHECK(strcmp(QA_LMCTF_GRAPPLE.name, "lmctf:hook") == 0 && QA_LMCTF_GRAPPLE.repeating);
}

static void test_entity_flare(void) {
  qa_q2_spawn_fields fields = {0};
  fields.ordinal = 0;
  CHECK(qa_q2_spawn_set(&fields, "classname", "light"));
  qa_q2_entity e;
  qa_q2_entity_init(&e, (qa_owned_actor){0}, fields);
  qa_scene_flare flare;
  qa_vec3 rim;
  CHECK(!qa_q2_entity_flare(&e, &flare, &rim));
  e.render_flags = 0x200000;
  CHECK(qa_q2_entity_flare(&e, &flare, &rim));
  CHECK(strcmp(flare.image, "misc/flare.tga") == 0);
  CHECK(flare.color.x == 255 && flare.fade_start == 96 && flare.fade_end == 384);
  e.skin = (double)0x11223344u;
  CHECK(qa_q2_entity_flare(&e, &flare, &rim));
  CHECK(flare.color.x == 0x11 && flare.color.y == 0x22 && flare.color.z == 0x33);
  qa_q2_entity_destroy(&e);
}

int main(void) {
  test_fields();
  test_start_items();
  test_effect_resources_and_attachments();
  test_held_weapons();
  test_callbacks();
  test_checkpoint_attack();
  test_items_catalog();
  test_unrotate_and_grapple();
  test_entity_flare();
  if (failures) {
    fprintf(stderr, "%d/%d checks failed\n", failures, checks);
    return 1;
  }
  printf("ok %d checks\n", checks);
  return 0;
}
