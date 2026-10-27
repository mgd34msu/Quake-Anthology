/*
 * Port of key cases from tests/gameplay/q1-addons/common.test.ts,
 * tests/gameplay/q1-addons/ctf/ctf.test.ts and
 * tests/gameplay/q1-addons/items/items.test.ts against a fake
 * Q1EntityServices host (random fixed at 0.4, as in the TS harness).
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/content/q1/addons.h"

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
      fprintf(stderr, "FAIL %s:%d: expected abort\n", __FILE__, __LINE__); \
    }                                                                   \
  } while (0)

#define MAX_ENTITIES 64
#define MAX_ITEMS 32

typedef struct {
  qa_q1ad_actor actor;
  qa_q1ad_body body;
  bool live;
  double health;
  char pending[QA_Q1AD_NAME];
} ent;

typedef struct {
  ent ents[MAX_ENTITIES];
  size_t ent_count;
  int64_t next_slot;
  qa_session_id session;
  qa_q1ad_player player_state;
  qa_actor_id player_id;
  qa_owned_actor player_owned;
  double campaign_flags;
  double skill_cvar;
  struct { const char *item; double count, capacity; } items[MAX_ITEMS];
  size_t item_count;
  int use_targets_calls, foreign_uses, messages, rune_events;
  const char *selected;
  qa_q1ad_ctf_status last_status;
  bool has_status;
  double captures_red, captures_blue;
  const char *team;
} host;

static qa_actor_id make_id(host *h) {
  qa_actor_id id = {.session = h->session, .slot = h->next_slot++, .generation = 1, .token = h};
  return id;
}
static ent *find_ent(host *h, qa_q1ad_actor *actor) {
  for (size_t i = 0; i < h->ent_count; i++) if (&h->ents[i].actor == actor) return &h->ents[i];
  return NULL;
}
static ent *ent_by_id(host *h, qa_actor_id id) {
  for (size_t i = 0; i < h->ent_count; i++)
    if (qa_same_actor(h->ents[i].actor.actor.id, id)) return &h->ents[i];
  return NULL;
}
static ent *new_ent(host *h, const char *classname) {
  ent *e = &h->ents[h->ent_count++];
  memset(e, 0, sizeof *e);
  e->live = true;
  e->health = 100;
  e->actor.actor = (qa_owned_actor){.id = make_id(h)};
  e->actor.classname = classname;
  e->actor.target = "";
  e->actor.targetname = "";
  e->actor.killtarget = "";
  e->actor.model = "";
  e->actor.message = "";
  e->actor.solid = "none";
  e->actor.movement = "none";
  e->body.bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
  return e;
}

static void h_remove(void *ctx, qa_q1ad_actor *entity) {
  ent *e = find_ent(ctx, entity);
  if (e != NULL) e->live = false;
}
static bool h_live(void *ctx, qa_q1ad_actor *entity) {
  ent *e = find_ent(ctx, entity);
  return e != NULL && e->live;
}
static qa_q1ad_actor *h_create(void *ctx, const char *classname) {
  return &new_ent(ctx, classname)->actor;
}
static qa_q1ad_actor *h_entity_by_id(void *ctx, qa_actor_id id) {
  ent *e = ent_by_id(ctx, id);
  return e != NULL ? &e->actor : NULL;
}
static size_t h_entities(void *ctx, qa_q1ad_actor **out, size_t capacity) {
  host *h = ctx;
  size_t n = 0;
  for (size_t i = 0; i < h->ent_count && n < capacity; i++) out[n++] = &h->ents[i].actor;
  return n;
}
static size_t h_find(void *ctx, const char *targetname, qa_q1ad_actor **out, size_t capacity) {
  host *h = ctx;
  size_t n = 0;
  for (size_t i = 0; i < h->ent_count && n < capacity; i++)
    if (h->ents[i].actor.targetname != NULL && strcmp(h->ents[i].actor.targetname, targetname) == 0)
      out[n++] = &h->ents[i].actor;
  return n;
}
static size_t h_players(void *ctx, qa_actor_id *out, size_t capacity) {
  host *h = ctx;
  if (capacity == 0) return 0;
  out[0] = h->player_id;
  return 1;
}
static bool h_resolve_owned(void *ctx, qa_actor_id id, qa_owned_actor *out) {
  host *h = ctx;
  ent *e = ent_by_id(h, id);
  if (e != NULL) { *out = e->actor.actor; return true; }
  if (qa_same_actor(id, h->player_id)) { *out = h->player_owned; return true; }
  return false;
}
static bool h_resolve_saved(void *ctx, int64_t slot, int64_t generation, qa_owned_actor *out) {
  host *h = ctx;
  for (size_t i = 0; i < h->ent_count; i++)
    if (h->ents[i].actor.actor.id.slot == slot && h->ents[i].actor.actor.id.generation == generation) {
      *out = h->ents[i].actor.actor;
      return true;
    }
  if (h->player_id.slot == slot && h->player_id.generation == generation) {
    *out = h->player_owned;
    return true;
  }
  return false;
}
static bool h_is_player(void *ctx, qa_actor_id id) {
  host *h = ctx;
  return qa_same_actor(id, h->player_id);
}
static qa_q1ad_player *h_player(void *ctx, qa_actor_id id) {
  host *h = ctx;
  return qa_same_actor(id, h->player_id) ? &h->player_state : NULL;
}
static double h_health(void *ctx, qa_actor_id id) {
  host *h = ctx;
  if (qa_same_actor(id, h->player_id)) return 100;
  ent *e = ent_by_id(h, id);
  return e != NULL ? e->health : 0;
}
static bool h_body(void *ctx, qa_q1ad_actor *entity, qa_q1ad_body *out) {
  ent *e = find_ent(ctx, entity);
  if (e == NULL) return false;
  *out = e->body;
  return true;
}
static bool h_body_of(void *ctx, qa_actor_id id, qa_q1ad_body *out) {
  host *h = ctx;
  if (qa_same_actor(id, h->player_id)) {
    *out = (qa_q1ad_body){.bounds = {{-16, -16, -24}, {16, 16, 32}}};
    return true;
  }
  ent *e = ent_by_id(h, id);
  if (e == NULL) return false;
  *out = e->body;
  return true;
}
static void h_set_body(void *ctx, qa_q1ad_actor *entity, const qa_q1ad_body_patch *patch) {
  ent *e = find_ent(ctx, entity);
  if (e == NULL) return;
  if (patch->has_origin) e->body.origin = patch->origin;
  if (patch->has_angles) e->body.angles = patch->angles;
  if (patch->has_velocity) e->body.velocity = patch->velocity;
  if (patch->has_bounds) e->body.bounds = patch->bounds;
  if (patch->has_ground) { e->body.ground = patch->ground; e->body.has_ground = !patch->ground_null; }
}
static void h_set_bounds(void *ctx, qa_q1ad_actor *entity, qa_bounds bounds) {
  ent *e = find_ent(ctx, entity);
  if (e != NULL) e->body.bounds = bounds;
}
static void h_set_origin(void *ctx, qa_q1ad_actor *entity, qa_vec3 origin) {
  ent *e = find_ent(ctx, entity);
  if (e != NULL) e->body.origin = origin;
}
static bool h_trace(void *ctx, qa_vec3 start, qa_vec3 end, qa_bounds bounds,
                    const qa_actor_id *ignore, bool monsters, qa_q1ad_trace *out) {
  (void)ctx; (void)start; (void)bounds; (void)ignore; (void)monsters;
  *out = (qa_q1ad_trace){.fraction = 1, .end = end};
  return true;
}
static double h_random(void *ctx) { (void)ctx; return 0.4; }
static void h_schedule(void *ctx, qa_q1ad_actor *entity, double delay, const char *name) {
  (void)delay;
  ent *e = find_ent(ctx, entity);
  if (e != NULL) (void)snprintf(e->pending, sizeof e->pending, "%s", name);
}
static void h_cancel(void *ctx, qa_q1ad_actor *entity) {
  ent *e = find_ent(ctx, entity);
  if (e != NULL) e->pending[0] = '\0';
}
static void h_use_targets(void *ctx, qa_q1ad_actor *entity, const qa_actor_id *activator) {
  host *h = ctx;
  (void)entity; (void)activator;
  h->use_targets_calls++;
}
static void h_call_use(void *ctx, qa_q1ad_actor *entity, const char *name,
                       const qa_actor_id *other, const qa_actor_id *activator) {
  host *h = ctx;
  (void)entity; (void)name; (void)other; (void)activator;
  h->foreign_uses++;
}
static void h_message(void *ctx, qa_actor_id player, const char *key, bool localized,
                      const qa_q1ad_msg_arg *args, size_t count) {
  host *h = ctx;
  (void)player; (void)key; (void)localized; (void)args; (void)count;
  h->messages++;
}
static double h_read_flags(void *ctx) { return ((host *)ctx)->campaign_flags; }
static void h_write_flags(void *ctx, double flags) { ((host *)ctx)->campaign_flags = flags; }
static double inv_count(host *h, const char *item) {
  for (size_t i = 0; i < h->item_count; i++)
    if (strcmp(h->items[i].item, item) == 0) return h->items[i].count;
  return 0;
}
static double h_inv_count(void *ctx, qa_actor_id actor, qa_item_id item) {
  (void)actor;
  return inv_count(ctx, item);
}
static void h_inv_configure(void *ctx, qa_owned_actor actor, qa_item_id item, double count, double capacity) {
  host *h = ctx;
  (void)actor;
  for (size_t i = 0; i < h->item_count; i++)
    if (strcmp(h->items[i].item, item) == 0) { h->items[i].count = count; h->items[i].capacity = capacity; return; }
  /* Item strings are either literals or the caller's static buffers in
   * these tests; weapon items are compared through stable copies. */
  static char copies[32][64];
  (void)snprintf(copies[h->item_count], 64, "%s", item);
  h->items[h->item_count].item = copies[h->item_count];
  h->items[h->item_count].count = count;
  h->items[h->item_count].capacity = capacity;
  h->item_count++;
}
static void h_inv_give(void *ctx, qa_owned_actor actor, qa_item_id item, double amount) {
  host *h = ctx;
  double held = inv_count(h, item);
  h_inv_configure(ctx, actor, item, held + amount, 1);
}
static size_t h_inv_entries(void *ctx, qa_actor_id actor, qa_q1ad_inventory_entry *out, size_t capacity) {
  host *h = ctx;
  (void)actor;
  size_t n = h->item_count < capacity ? h->item_count : capacity;
  for (size_t i = 0; i < n; i++)
    out[i] = (qa_q1ad_inventory_entry){h->items[i].item, h->items[i].count, h->items[i].capacity};
  return n;
}
static bool h_select_weapon(void *ctx, qa_owned_actor actor, const char *weapon) {
  host *h = ctx;
  (void)actor;
  h->selected = weapon;
  h->player_state.weapon = weapon;
  return true;
}
static const char *h_weapon_ammo(void *ctx, const char *weapon) {
  (void)ctx;
  if (strcmp(weapon, "shotgun") == 0 || strcmp(weapon, "supershotgun") == 0) return "q1:ammo/shells";
  if (strcmp(weapon, "nailgun") == 0 || strcmp(weapon, "supernailgun") == 0) return "q1:ammo/nails";
  if (strcmp(weapon, "grenadelauncher") == 0 || strcmp(weapon, "rocketlauncher") == 0) return "q1:ammo/rockets";
  if (strcmp(weapon, "lightning") == 0 || strcmp(weapon, "mg3:laser") == 0) return "q1:ammo/cells";
  return NULL;
}
static const char *h_combat_team(void *ctx, qa_actor_id actor) {
  (void)actor;
  return ((host *)ctx)->team;
}
static void h_set_team(void *ctx, qa_owned_actor actor, const char *team) {
  (void)actor;
  ((host *)ctx)->team = team;
}
static double h_cvar(void *ctx, const char *name) {
  host *h = ctx;
  return strcmp(name, "skill") == 0 ? h->skill_cvar : 0;
}
static void h_set_cvar(void *ctx, const char *name, const char *value) {
  host *h = ctx;
  if (strcmp(name, "skill") == 0) h->skill_cvar = strtod(value, NULL);
}
static void h_emit(void *ctx, const qa_q1ad_event *event) {
  if (event->kind == QA_Q1AD_EVENT_RUNE_COLLECTED) ((host *)ctx)->rune_events++;
}
static void h_status(void *ctx, qa_actor_id actor, qa_q1ad_ctf_status status) {
  host *h = ctx;
  (void)actor;
  h->last_status = status;
  h->has_status = true;
}
static double h_captures(void *ctx, const char *team) {
  host *h = ctx;
  return strcmp(team, "red") == 0 ? h->captures_red : h->captures_blue;
}

static void setup(host *h, qa_q1ad_context *context, qa_q1ad_program program) {
  memset(h, 0, sizeof *h);
  h->session = (qa_session_id){"q1-addon-smoke", h};
  h->next_slot = 1;
  h->player_id = make_id(h);
  h->player_owned = (qa_owned_actor){.id = h->player_id};
  h->player_state.actor = h->player_owned;
  h->player_state.weapon = "axe";
  h->player_state.max_health = 100;
  qa_q1ad_services services = {.ctx = h, .emit = h_emit, .cvar = h_cvar, .set_cvar = h_set_cvar};
  qa_q1ad_game game = {.ctx = h, .skill = 1, .map_name = "start",
      .remove = h_remove, .live = h_live, .create = h_create,
      .entity_by_id = h_entity_by_id, .entities = h_entities, .find = h_find,
      .players = h_players, .resolve_owned = h_resolve_owned, .resolve_saved = h_resolve_saved,
      .is_player = h_is_player, .player = h_player, .health = h_health,
      .body = h_body, .body_of = h_body_of, .set_body = h_set_body,
      .set_bounds = h_set_bounds, .set_origin = h_set_origin, .trace = h_trace,
      .random = h_random, .message = h_message, .use_targets = h_use_targets,
      .schedule = h_schedule, .cancel = h_cancel, .call_use = h_call_use,
      .inventory_count = h_inv_count, .inventory_configure = h_inv_configure,
      .inventory_give = h_inv_give,
      .inventory_entries = h_inv_entries, .combat_team = h_combat_team,
      .combat_set_team = h_set_team, .select_weapon = h_select_weapon,
      .weapon_ammo = h_weapon_ammo, .campaign_read_flags = h_read_flags,
      .campaign_write_flags = h_write_flags};
  (void)qa_q1ad_register_q1_campaign_addons(context, program, services, game);
}
static ent *spawn(host *h, qa_q1ad_context *context, const char *classname) {
  ent *e = new_ent(h, classname);
  (void)qa_q1ad_spawn_dispatch(context, &e->actor);
  return e;
}
static void think(host *h, qa_q1ad_context *context, ent *e) {
  char name[QA_Q1AD_NAME];
  (void)snprintf(name, sizeof name, "%s", e->pending);
  (void)h;
  e->pending[0] = '\0';
  if (name[0] != '\0') qa_q1ad_do_action(context, &e->actor, name);
}

static void test_sigils(void) {
  for (int mg3 = 0; mg3 < 2; mg3++) {
    host h;
    qa_q1ad_context context;
    setup(&h, &context, mg3 ? QA_Q1AD_MG3 : QA_Q1AD_MG1);
    ent *rune = new_ent(&h, "item_sigil");
    rune->actor.spawnflags = mg3 ? 136 : 16;
    (void)qa_q1ad_spawn_dispatch(&context, &rune->actor);
    CHECK(strcmp(rune->actor.model, mg3 ? "progs/end4.mdl" : "progs/mg1_rune5.mdl") == 0);
    qa_q1ad_do_touch(&context, &rune->actor, rune->actor.touch, &h.player_id, NULL);
    CHECK(h.campaign_flags == (mg3 ? 8 : 1040));
    CHECK(qa_q1ad_mg1_last_sigil(h.campaign_flags) == (mg3 ? 0 : 16));
    CHECK(qa_q1ad_mg3_rune_count(h.campaign_flags) == (mg3 ? 1 : 0));
    CHECK(strcmp(rune->actor.solid, "none") == 0);
    CHECK(h.rune_events == 1);
    qa_q1ad_context_destroy(&context);
  }
}

static void test_counter_and_multitouch(void) {
  host h;
  qa_q1ad_context context;
  setup(&h, &context, QA_Q1AD_MG1);
  ent *counter = new_ent(&h, "trigger_counter_timed");
  counter->actor.count = 2;
  counter->actor.delay = 2;
  (void)qa_q1ad_spawn_dispatch(&context, &counter->actor);
  qa_q1ad_do_use(&context, &counter->actor, counter->actor.use, NULL, &h.player_id);
  CHECK(counter->actor.count == 1);
  think(&h, &context, counter);
  CHECK(counter->actor.count == 2);
  qa_q1ad_do_use(&context, &counter->actor, counter->actor.use, NULL, &h.player_id);
  qa_q1ad_do_use(&context, &counter->actor, counter->actor.use, NULL, &h.player_id);
  CHECK(h.foreign_uses == 1);
  CHECK(!counter->live);
  ent *trigger = spawn(&h, &context, "trigger_multitouch");
  qa_q1ad_do_touch(&context, &trigger->actor, trigger->actor.touch, &h.player_id, NULL);
  qa_q1ad_do_touch(&context, &trigger->actor, trigger->actor.touch, &h.player_id, NULL);
  CHECK(h.use_targets_calls == 1);
  think(&h, &context, trigger);
  CHECK(h.use_targets_calls == 2);
  qa_q1ad_do_touch(&context, &trigger->actor, trigger->actor.touch, &h.player_id, NULL);
  CHECK(h.use_targets_calls == 3);
  qa_q1ad_context_destroy(&context);
}

static void test_impulses(void) {
  host h;
  qa_q1ad_context context;
  setup(&h, &context, QA_Q1AD_MG1);
  CHECK(qa_q1ad_handle_impulse(&context, h.player_id, 99));
  CHECK(inv_count(&h, "q1:key/silver") == 0);
  CHECK(inv_count(&h, "q1:ammo/cells") == 200);
  CHECK(strcmp(h.player_state.weapon, "rocketlauncher") == 0);
  qa_q1ad_context_destroy(&context);

  setup(&h, &context, QA_Q1AD_MG3);
  CHECK(qa_q1ad_handle_impulse(&context, h.player_id, 105));
  CHECK(qa_q1ad_handle_impulse(&context, h.player_id, 223));
  CHECK(h.campaign_flags == (15 | 64 | 128));
  CHECK(h.skill_cvar == 3);
  h.campaign_flags = 64 | 128 | 8;
  CHECK(qa_q1ad_handle_impulse(&context, h.player_id, 11));
  CHECK(h.campaign_flags == (64 | 128 | 8 | 1));
  qa_q1ad_context_destroy(&context);
}

static double fixed_random(void *ctx) { (void)ctx; return 0.4; }

static void test_pure_helpers(void) {
  CHECK(qa_q1ad_light_style(0.5, false) == 'g');
  CHECK(qa_q1ad_light_style(0, false) == 'a');
  CHECK(qa_q1ad_light_style(1, true) == 'z');
  qa_q1ad_squad_spawn spawns[4];
  CHECK(qa_q1ad_horde_squad("3 grunts", 1, fixed_random, NULL, spawns, 4) == 3);
  CHECK(strcmp(spawns[0].monster, "grunt") == 0 && spawns[0].offset.y == -40);
  CHECK(qa_q1ad_horde_squad("3 grunts", 0, fixed_random, NULL, spawns, 4) == 2);
  const char *squad = NULL, *type = NULL;
  CHECK(qa_q1ad_choose_horde_squad(true, "fodder", fixed_random, NULL, &squad, &type));
  CHECK(strcmp(squad, "2 grunts, 1 dog") == 0 && strcmp(type, "normal") == 0);
  CHECK(!qa_q1ad_choose_horde_squad(true, "bosses", fixed_random, NULL, &squad, &type));
  CHECK(qa_q1ad_mg3_upgrade_flag("map2b") == 8192);
  CHECK(qa_q1ad_mg3_upgrade_flag("map1") == 1);
  CHECK(qa_q1ad_mg3_upgrade_flag("secret6") == 16384);
  CHECK(qa_q1ad_mg3_upgrade_flag("nowhere") == 0);
  CHECK(qa_q1ad_mg3_upgraded_maximum(50, 5) == 70);
  CHECK(qa_q1ad_mg3_weapon_rank("lightning") == 1);
  CHECK(qa_q1ad_mg3_weapon_rank("axe") == 8);
  CHECK(qa_q1ad_ctf_weapon_rank("nailgun") == 6);
  CHECK(qa_q1ad_ctf_team_number("red") == 5);
  CHECK(strcmp(qa_q1ad_ctf_number_team(14), "blue") == 0);
}

static void test_horde_prepare(void) {
  host h;
  qa_q1ad_context context;
  setup(&h, &context, QA_Q1AD_MG1);
  qa_q1ad_horde horde;
  qa_q1ad_horde_services services = {0};
  qa_q1ad_horde_init(&horde, &context, services);
  ent *manager = spawn(&h, &context, "horde_manager");
  CHECK(qa_q1ad_horde_prepare(&horde, &manager->actor));
  CHECK(qa_q1ad_actor_number(&manager->actor, "fodder") == 3);
  CHECK(qa_q1ad_actor_number(&manager->actor, "army") == 1);
  CHECK(qa_q1ad_actor_number(&manager->actor, "wave") == 1);
  qa_q1ad_horde_change_keys(&horde, "silver", 1);
  CHECK(inv_count(&h, "q1:key/silver") == 1);
  qa_q1ad_horde_change_keys(&horde, "silver", 1);
  CHECK(qa_q1ad_actor_number(&manager->actor, "keys_silver") == 2);
  qa_q1ad_context_destroy(&context);
}

static void test_ctf_update(void) {
  host h;
  qa_q1ad_context context;
  setup(&h, &context, QA_Q1AD_CTF);
  qa_q1ad_ctf_services services = {.ctx = &h, .status = h_status, .captures = h_captures};
  qa_q1ad_ctf ctf = {0};
  qa_q1ad_ctf_init(&ctf, &context, services, NULL);
  ent *red = new_ent(&h, "item_flag_team1");
  ent *blue = new_ent(&h, "item_flag_team2");
  red->actor.count = 0;
  blue->actor.count = 2;
  h.captures_red = 3;
  h.captures_blue = 1;
  qa_q1ad_ctf_update(&ctf, NULL);
  CHECK(h.has_status);
  CHECK(h.last_status.red == 3 && h.last_status.blue == 1);
  CHECK(h.last_status.flags == (1 | (4 << 3)));
  CHECK(h.last_status.rune_items == 0);
  qa_q1ad_context_destroy(&context);
}

static void test_context_words_and_aborts(void) {
  host h;
  qa_q1ad_context context;
  setup(&h, &context, QA_Q1AD_MG3);
  qa_q1ad_set_player_number(&context, h.player_id, "score", 42);
  CHECK(qa_q1ad_player_number(&context, h.player_id, "score") == 42);
  size_t length = 0;
  uint8_t *bytes = qa_q1ad_context_capture(&context, &length);
  CHECK(bytes != NULL && length > 0);
  qa_q1ad_set_player_number(&context, h.player_id, "score", 1);
  qa_q1ad_context_restore(&context, bytes, length);
  CHECK(qa_q1ad_player_number(&context, h.player_id, "score") == 42);
  free(bytes);
  qa_q1ad_context_release(&context, h.player_owned);
  CHECK(qa_q1ad_player_number(&context, h.player_id, "score") == 0);
  qa_actor_id unknown = {.session = h.session, .slot = 999, .generation = 1, .token = &h};
  CHECK_ABORTS(qa_q1ad_set_player_number(&context, unknown, "x", 1));
  ent *corpse = new_ent(&h, "misc_corpse");
  qa_q1ad_actor_set(&corpse->actor, "style", "99");
  CHECK_ABORTS((void)qa_q1ad_spawn_dispatch(&context, &corpse->actor));
  ent *exit_trigger = new_ent(&h, "trigger_changelevel");
  CHECK_ABORTS((void)qa_q1ad_spawn_dispatch(&context, &exit_trigger->actor));
  qa_q1ad_context_destroy(&context);
}

int main(void) {
  test_sigils();
  test_counter_and_multitouch();
  test_impulses();
  test_pure_helpers();
  test_horde_prepare();
  test_ctf_update();
  test_context_words_and_aborts();
  if (failures == 0) printf("ok: %d checks\n", checks);
  return failures == 0 ? 0 : 1;
}
