/*
 * C checks for src/content/q1/missionpacks/monsters over the one Q1 game: a
 * real qa_q1_services on the shared world tables (tests/content/
 * q1_world_fixture.h), qa_q1_register_base, the mission-pack arsenal and
 * qa_q1mpm_register_mission_pack_monsters, as the TS sessions build them.
 * Ported from ../quake-anthology-ts/tests/gameplay/q1-missionpacks/monsters/
 * source.test.ts and monsters/bosses/source.test.ts on the retail maps (entity
 * lumps read from the installed paks; skipped when absent). The TS cases trace
 * through the map's scene and move Armagon/Dragon with Q1MonsterMovement; the
 * fixture's open world (fraction 1, walkMove false) stands in, so every
 * expected value below comes from the same scenarios run in TS under bun over
 * an equal open-world host (scratchpad/mpm/oracle.ts; printed lines quoted).
 * The saved-session halves of the boss cases (Q1 foundation checkpoint +
 * shared tables round trip) are covered here by continuing the live session.
 * GPL-2.0-or-later.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "q1_world_fixture.h"
#include "qa/content/q1/base/creatures.h"
#include "qa/content/q1/base/frames.h"
#include "qa/content/q1/base/provider.h"
#include "qa/content/q1/foundation/checkpoint.h"
#include "qa/content/q1/missionpacks/arsenal.h"
#include "qa/content/q1/missionpacks/monsters.h"
#include "qa/content/q1/missionpacks/world/hipnotic_misc.h"
#include "qa/formats/q1_map/entities.h"
#include "qa/foundation/js.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* --- retail map entity lumps (PACK directory + BSP lump 0) --- */
static const char *corpus_root(void) {
  const char *root = getenv("QTS_TEST_CORPUS");
  if (root == NULL) root = getenv("QFILES_ROOT");
  return root;
}
static uint8_t *read_file(const char *path, size_t *length) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) return NULL;
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);
  uint8_t *bytes = size > 0 ? malloc((size_t)size) : NULL;
  if (bytes != NULL && fread(bytes, 1, (size_t)size, file) != (size_t)size) { free(bytes); bytes = NULL; }
  fclose(file);
  *length = bytes == NULL ? 0 : (size_t)size;
  return bytes;
}
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
/* Steam layout: <root>/Quake[/rerelease]/<pack>/pak0.pak. NULL entities = absent. */
static qa_q1_entity *map_entities(const char *pack, const char *map, const char *edition, size_t *count) {
  *count = 0;
  const char *root = corpus_root();
  if (root == NULL) return NULL;
  char path[512], name[64];
  snprintf(path, sizeof path, "%s/Quake/%s%s/pak0.pak", root, strcmp(edition, "rerelease") == 0 ? "rerelease/" : "", pack);
  snprintf(name, sizeof name, "maps/%s.bsp", map);
  size_t length;
  uint8_t *pak = read_file(path, &length);
  if (pak == NULL || length < 12 || memcmp(pak, "PACK", 4) != 0) { free(pak); return NULL; }
  const uint32_t directory = le32(pak + 4), directory_length = le32(pak + 8);
  qa_q1_entity *entities = NULL;
  for (uint32_t at = directory; at + 64 <= directory + directory_length && at + 64 <= length; at += 64) {
    if (strncmp((const char *)pak + at, name, 56) != 0) continue;
    const uint8_t *bsp = pak + le32(pak + at + 56);
    const uint32_t offset = le32(bsp + 4), size = le32(bsp + 8);
    char *text = malloc((size_t)size + 1);
    memcpy(text, bsp + offset, size);
    text[size] = '\0';
    entities = qa_q1_parse_entities(text, count);
    free(text);
    break;
  }
  free(pak);
  return entities;
}

/* --- session: TS session(pack, map, edition) over the fixture --- */
static char *dup_text(const char *text) {
  const size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  memcpy(copy, text, length);
  return copy;
}
static int sentinel_charmer;
typedef struct session {
  q1_world_fixture fx;
  qa_q1_base *base;
  qa_q1mp_mission_pack_arsenal arsenal;
  qa_q1mpm_runtime monsters;
  qa_q1_entity *entities; size_t entity_count;
  qa_vec3 start;
} session;
static bool hooks_charmer(void *ctx, qa_actor_id *out) { (void)out; CHECK(ctx == &sentinel_charmer); return false; }
static bool spawn_standard(const char *c) {
  return strcmp(c, "worldspawn") == 0 || strncmp(c, "monster_", 8) == 0 || strcmp(c, "path_corner") == 0 ||
         strcmp(c, "info_player_start") == 0;
}
static bool spawn_bosses(const char *c) {
  return strcmp(c, "worldspawn") == 0 || strncmp(c, "monster_", 8) == 0 || strcmp(c, "dragon_corner") == 0 ||
         strcmp(c, "path_corner") == 0;
}
static bool session_open(session *s, const char *pack, const char *map, const char *edition, bool (*spawn)(const char *), bool misc) {
  memset(s, 0, sizeof *s);
  s->entities = map_entities(pack, map, edition, &s->entity_count);
  if (s->entities == NULL) return false;
  qa_q1_options options = q1_world_fixture_options();
  options.edition = edition;
  options.has_teamplay = true;
  options.teamplay = 0;
  options.max_clients = 2;
  options.campaign = strcmp(pack, "hipnotic") == 0 ? "q1:hipnotic" : "q1:rogue";
  options.movement_provider = "q3:movement";
  CHECK(q1_world_fixture_create(&s->fx, options));
  qa_q1_services *game = s->fx.game;
  s->base = qa_q1_register_base(game, NULL);
  (void)qa_q1mp_register_mission_pack_arsenal(game, pack, &s->arsenal);
  const qa_q1mpm_mission_monster_hooks hooks = {&sentinel_charmer, hooks_charmer};
  CHECK(qa_q1mpm_register_mission_pack_monsters(game, s->base, pack, &hooks, &s->monsters) == &s->monsters);
  if (misc) qa_q1mp_register_hipnotic_misc(game);
  for (size_t i = 0; i < s->entity_count; i++) {
    const char *classname = qa_q1_entity_value(&s->entities[i], "classname");
    if (classname == NULL) classname = "";
    if (spawn(classname)) qa_q1_spawn_entity(game, qa_q1_create(game, classname, &s->entities[i], NULL));
  }
  for (size_t i = 0; i < game->entity_count; i++)
    if (strcmp(game->entities[i]->classname, "info_player_start") == 0) { s->start = qa_q1_get_body(game, game->entities[i]).origin; break; }
  return true;
}
static qa_q1_player_state *session_player(session *s, double health) {
  const double slot = (double)s->fx.player_count + 1;
  const qa_owned_actor owner = q1_world_fixture_admit_player(&s->fx, qa_q1_vadd(s->start, (qa_vec3){64 * (slot - 1), 0, 0}), health);
  return qa_q1_attach_player(s->fx.game, owner, "shotgun", true, NULL);
}
/* TS tick(entity): run the entity's scheduled source callback now. */
static void session_tick(session *s, qa_q1_actor *entity) {
  qa_q1_services *game = s->fx.game;
  const qa_q1_bound_action callback = entity->think;
  CHECK(callback.fn != NULL);
  if (callback.fn == NULL) return;
  qa_q1_services_set_time(game, entity->next_think);
  entity->think = (qa_q1_bound_action){0};
  entity->next_think = -1;
  game->host.cancel_think(game->host.ctx, entity->actor);
  qa_q1_call_action(game, entity, callback);
}
static void session_close(session *s) {
  CHECK(s->fx.ctx_errors == 0);
  qa_q1mpm_runtime_destroy(&s->monsters);
  qa_q1mp_mission_pack_arsenal_destroy(&s->arsenal);
  qa_q1_base_destroy(s->base);
  q1_world_fixture_destroy(&s->fx);
  qa_q1_entities_destroy(s->entities, s->entity_count);
}
static qa_q1mpm_mission_monster *find_species(session *s, const char *species) {
  for (size_t i = 0; i < s->monsters.monster_count; i++)
    if (strcmp(s->monsters.monsters[i].monster->base.spec->species, species) == 0) return s->monsters.monsters[i].monster;
  return NULL;
}
static size_t count_class(qa_q1_services *game, const char *classname) {
  size_t count = 0;
  for (size_t i = 0; i < game->entity_count; i++) count += strcmp(game->entities[i]->classname, classname) == 0;
  return count;
}
static bool definition_has_action(const qa_q1mpm_pack_monster_definition *definition, const char *name) {
  for (size_t i = 0; i < definition->action_count; i++)
    if (strcmp(definition->actions[i].name, name) == 0) return true;
  return false;
}
static bool capture_ok(qa_q1_services *game) {
  qa_q1_foundation_checkpoint checkpoint;
  if (!qa_q1_services_capture(game, &checkpoint)) return false;
  qa_q1_checkpoint_free(&checkpoint);
  return true;
}
static bool same_capture(const qa_q1_base_monster *a, const qa_q1_base_monster *b) {
  qa_save_value *left = qa_q1_base_monster_capture(a), *right = qa_q1_base_monster_capture(b);
  size_t left_length = 0, right_length = 0;
  uint8_t *left_bytes = qa_encode_checkpoint_value(left, &left_length);
  uint8_t *right_bytes = qa_encode_checkpoint_value(right, &right_length);
  const bool same = left_length == right_length && memcmp(left_bytes, right_bytes, left_length) == 0;
  free(left_bytes); free(right_bytes);
  qa_save_value_free(left); qa_save_value_free(right);
  return same;
}

/* "retail Hipnotic and Rogue monsters admit exact frames and saved continuations".
 * bun: counts {classic,rerelease} hip2m1: gremlin=6 missionMonsters=68 entities=101 totalMonsters=68 consistent=true
 *      counts {classic,rerelease} r2m1: eel=13 missionMonsters=102 entities=124 totalMonsters=108 consistent=true
 * (r2m1 re-derived for monsters/species-variants.ts: the Rogue id1 species are mission monsters
 * and the three untargeted statues are not counted; scratchpad/mp/variants.ts of the q1-sync-mp
 * agent. The TS check now also accepts a continuation into the shared monsterFrames.) */
static void test_retail_counts(void) {
  static const char *const editions[] = {"classic", "rerelease"};
  for (size_t e = 0; e < 2; e++) {
    static const struct { const char *pack, *map, *species; size_t count, missions, entities; double total; } rows[] = {
        {"hipnotic", "hip2m1", "gremlin", 6, 68, 101, 68}, {"rogue", "r2m1", "eel", 13, 102, 124, 108}};
    for (size_t r = 0; r < 2; r++) {
      session s;
      if (!session_open(&s, rows[r].pack, rows[r].map, editions[e], spawn_standard, false)) {
        printf("skip retail %s %s (corpus absent)\n", rows[r].map, editions[e]);
        continue;
      }
      size_t species = 0;
      bool consistent = true;
      for (size_t i = 0; i < s.monsters.monster_count; i++) {
        const qa_q1mpm_mission_monster *monster = s.monsters.monsters[i].monster;
        species += strcmp(monster->base.spec->species, rows[r].species) == 0;
        const qa_q1_monster_frame_table *frames = &monster->definition->frames;
        for (size_t f = 0; f < frames->count; f++) {
          const qa_q1_monster_frame *frame = &frames->entries[f].frame;
          if (!qa_q1_monster_frame_table_has(frames, frame->next) && !definition_has_action(monster->definition, frame->next) &&
              !qa_q1_monster_frames_has(frame->next))
            consistent = false;
          for (size_t o = 0; o < frame->operation_count; o++)
            if (frame->operations[o].kind == QA_Q1_OP_ACTION &&
                !definition_has_action(monster->definition, frame->operations[o].name))
              consistent = false;
        }
      }
      CHECK(species == rows[r].count);
      CHECK(s.monsters.monster_count == rows[r].missions);
      CHECK(s.fx.game->entity_count == rows[r].entities);
      CHECK(s.fx.game->total_monsters == rows[r].total);
      CHECK(consistent);
      CHECK(capture_ok(s.fx.game));
      session_close(&s);
    }
  }
}

/* "Gremlin steals the shared weapon, spends signed burst ammo and clones pending source state".
 * bun: steal: stole=true playerNailgun=0 gremlinNails=1 weapon=nailgun playerWeapon=shotgun events=17
 *      steal burst: nails=-3 currentammo=-3 nextFrame=gremlin_nail7 spikes=4
 *      steal clone: sameCapture=true cloneNails=-3 */
static void test_gremlin_steal(void) {
  session s;
  if (!session_open(&s, "hipnotic", "hip2m1", "rerelease", spawn_standard, false)) { printf("skip gremlin steal\n"); return; }
  qa_q1_services *game = s.fx.game;
  qa_shared_inventory_table *inventory = game->host.inventory;
  qa_q1_player_state *player = session_player(&s, 100);
  qa_q1_player_state *target = session_player(&s, 100);
  qa_q1mpm_mission_monster *monster = find_species(&s, "gremlin");
  CHECK(monster != NULL);
  if (monster == NULL) { session_close(&s); return; }
  const qa_vec3 origin = qa_q1_get_body(game, monster->base.entity).origin;
  qa_body_state body = qa_shared_body_table_read(game->host.bodies, player->actor.id).value;
  body.origin = qa_q1_vadd(origin, (qa_vec3){40, 0, 0});
  qa_shared_body_table_write(game->host.bodies, player->actor, body);
  qa_inventory_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.item = qa_q1_services_weapon_item(game, "nailgun"); entry.count = 1; entry.capacity = 1;
  qa_shared_inventory_table_configure(inventory, player->actor, entry, NULL, NULL);
  entry.item = "q1:ammo/nails"; entry.count = 1; entry.capacity = 200;
  qa_shared_inventory_table_configure(inventory, player->actor, entry, NULL, NULL);
  (void)qa_q1_select_weapon(game, player->actor, "nailgun");
  qa_q1_base_monster_set_enemy(&monster->base, &player->actor.id);
  s.fx.random_value = 0.7;
  CHECK(qa_q1mpm_gremlin_steal(monster));
  CHECK(qa_shared_inventory_table_count(inventory, player->actor.id, qa_q1_services_weapon_item(game, "nailgun")) == 0);
  CHECK(qa_shared_inventory_table_count(inventory, monster->base.entity->actor.id, "q1:ammo/nails") == 1);
  CHECK(strcmp(qa_q1_actor_field_get(monster->base.entity, "gremlin:weapon"), "nailgun") == 0);
  CHECK(strcmp(player->weapon, "shotgun") == 0);
  CHECK(s.fx.event_count == 17); /* every event since the session opened */
  qa_q1_base_monster_set_enemy(&monster->base, &target->actor.id);
  qa_q1_base_monster_play(&monster->base, "gremlin_nail3");
  for (int i = 0; i < 3; i++) {
    char next[64];
    snprintf(next, sizeof next, "%s", monster->base.next_frame);
    qa_q1_base_monster_play(&monster->base, next);
  }
  CHECK(qa_shared_inventory_table_count(inventory, monster->base.entity->actor.id, "q1:ammo/nails") == -3);
  CHECK(qa_q1_actor_number(monster->base.entity, "currentammo") == -3);
  CHECK(strcmp(monster->base.next_frame, "gremlin_nail7") == 0);
  CHECK(count_class(game, "spike") == 4);
  qa_q1_actor *clone = qa_q1_clone_entity(game, monster->base.entity);
  CHECK(same_capture(&qa_q1mpm_runtime_require(&s.monsters, clone)->base, &monster->base));
  CHECK(qa_shared_inventory_table_count(inventory, clone->actor.id, "q1:ammo/nails") == -3);
  CHECK(capture_ok(game));
  session_close(&s);
}

/* "Horn charm keeps retail soldier and dog callbacks on the shared controller".
 * bun: charm army: same=true charmer=true enemyNull=true hunting=2 nextFrame=army_walk1 walk=army_walk1
 *        goalIsField=true goalNonNull=true effects=8 think=set nextThink=0.05000000074505806
 *      charm dog: (same, nextFrame=dog_walk1) */
static void test_horn_charm(void) {
  session s;
  if (!session_open(&s, "hipnotic", "hip1m1", "rerelease", spawn_standard, false)) { printf("skip horn charm\n"); return; }
  qa_q1_services *game = s.fx.game;
  qa_q1_player_state *player = session_player(&s, 100);
  (void)session_player(&s, 100);
  static const char *const species[] = {"army", "dog"};
  for (size_t i = 0; i < 2; i++) {
    qa_q1mpm_mission_monster *monster = find_species(&s, species[i]);
    CHECK(monster != NULL);
    if (monster == NULL) continue;
    /* The Hipnotic base species run as baseBehavior MissionMonsters, so charm keeps the controller. */
    qa_save_value *before = qa_q1_base_monster_capture(&monster->base);
    size_t before_length = 0;
    uint8_t *before_bytes = qa_encode_checkpoint_value(before, &before_length);
    qa_q1mpm_runtime_charm(&s.monsters, monster->base.entity, player->actor.id);
    CHECK(qa_q1mpm_runtime_get(&s.monsters, monster->base.entity->actor) == monster);
    qa_save_value *after = qa_q1_base_monster_capture(&monster->base);
    size_t after_length = 0;
    uint8_t *after_bytes = qa_encode_checkpoint_value(after, &after_length);
    CHECK(before_length == after_length && memcmp(before_bytes, after_bytes, before_length) == 0);
    free(before_bytes); free(after_bytes); qa_save_value_free(before); qa_save_value_free(after);
    qa_actor_id charmer;
    CHECK(qa_q1_actor_reference_get(monster->base.entity, "charmer", &charmer) && qa_same_actor(charmer, player->actor.id));
    qa_q1_base_monster_found(&monster->base, player->actor.id);
    CHECK(qa_q1_base_monster_enemy(&monster->base) == NULL);
    qa_body_state body = qa_shared_body_table_read(game->host.bodies, player->actor.id).value;
    body.origin = qa_q1_vadd(qa_q1_base_monster_origin(&monster->base), (qa_vec3){300, 0, 0});
    qa_shared_body_table_write(game->host.bodies, player->actor, body);
    qa_q1_base_monster_ai(&monster->base, QA_Q1_AI_WALK, 2);
    CHECK(qa_q1_actor_number(monster->base.entity, "huntingcharmer") == 2);
    CHECK(strcmp(monster->base.next_frame, monster->base.spec->walk) == 0);
    qa_actor_id goal, field;
    CHECK(qa_q1_actor_reference_get(monster->base.entity, "goalentity", &goal));
    CHECK(qa_q1_actor_reference_get(monster->base.entity, "trigger_field", &field) && qa_same_actor(goal, field));
    CHECK(monster->base.entity->effects == 8);
    CHECK(monster->base.entity->next_think == (double)(float)0.05);
  }
  CHECK(capture_ok(game));
  session_close(&s);
}

/* "Hipnotic path corners keep authored waits on the monster controller".
 * bun: paths: path=test_second pauseUntil=7 time=0 goalIsSecond=true currentFrame=army_stand1 */
static void test_path_corners(void) {
  session s;
  if (!session_open(&s, "hipnotic", "hip1m1", "rerelease", spawn_standard, false)) { printf("skip path corners\n"); return; }
  qa_q1_services *game = s.fx.game;
  (void)session_player(&s, 100);
  (void)session_player(&s, 100);
  qa_q1mpm_mission_monster *monster = find_species(&s, "army");
  CHECK(monster != NULL);
  if (monster == NULL) { session_close(&s); return; }
  qa_q1_actor *first = qa_q1_create(game, "path_corner", NULL, NULL), *second = qa_q1_create(game, "path_corner", NULL, NULL);
  free(first->targetname); first->targetname = dup_text("test_first");
  free(first->target); first->target = dup_text("test_second");
  first->delay = 7;
  free(second->targetname); second->targetname = dup_text("test_second");
  qa_q1_spawn_entity(game, first);
  qa_q1_spawn_entity(game, second);
  free(monster->base.state->path); monster->base.state->path = dup_text(first->targetname);
  qa_q1_base_monster_set_enemy(&monster->base, NULL);
  qa_q1_call_touch(game, first, first->touch, monster->base.entity->actor.id, NULL, NULL);
  CHECK(strcmp(monster->base.state->path, "test_second") == 0);
  CHECK(monster->base.state->pause_until == game->time + 7 && game->time == 0);
  qa_actor_id goal;
  CHECK(qa_q1_actor_reference_get(monster->base.entity, "goalentity", &goal) && qa_same_actor(goal, second->actor.id));
  CHECK(strcmp(monster->base.current_frame, "army_stand1") == 0);
  CHECK(capture_ok(game));
  session_close(&s);
}

/* Emit double: counts "armagon/pain.wav" sounds as they are emitted (the
 * fixture keeps shallow event copies whose paths may point at a returned
 * caller's buffer), then records through the fixture's own emit. */
static size_t armagon_pain_sounds;
static void counting_emit(void *ctx, qa_q1_event event) {
  (void)q1_world_fixture_of(ctx);
  if (strcmp(event.kind, "sound") == 0 && event.path != NULL && strcmp(event.path, "armagon/pain.wav") == 0)
    armagon_pain_sounds++;
  q1_wf_emit(ctx, event);
}

/* "retail Armagon preserves split body, rocket bursts, pain cooldown and staged saved death".
 * bun (classic / rerelease): health=2500 bodyMovement=step / none entities=12
 *   attack: missilesBefore=0 missilesAfter=6 bodyFrameMatches=true frame=63 currentFrame=armagon_satk16
 *   pain: after24=0 after25and100=1
 *   death: ticks=13 think=null movement=none killed=1 multiExplosions=2 gorging=1
 *   body: cnt=7 count=6 gibs=9
 *   explode: bodyTicks=20 frame=0 movement=none solid=none */
static void test_armagon(void) {
  static const char *const editions[] = {"classic", "rerelease"};
  for (size_t e = 0; e < 2; e++) {
    session s;
    if (!session_open(&s, "hipnotic", "hipend", editions[e], spawn_bosses, true)) { printf("skip armagon %s\n", editions[e]); continue; }
    qa_q1_services *game = s.fx.game;
    game->host.emit = counting_emit;
    armagon_pain_sounds = 0;
    qa_q1_player_state *player = session_player(&s, 1000);
    qa_q1mpm_mission_monster *monster = find_species(&s, "armagon");
    CHECK(monster != NULL);
    if (monster == NULL) { session_close(&s); continue; }
    qa_actor_id body_id;
    CHECK(qa_q1_actor_reference_get(monster->base.entity, "trigger_field", &body_id));
    qa_q1_actor *body = qa_q1_get_entity(game, &body_id);
    CHECK(body != NULL);
    if (body == NULL) { session_close(&s); continue; }
    CHECK(qa_q1_health(game, monster->base.entity->actor.id) == 2500);
    CHECK(strcmp(body->movement, e == 0 ? "step" : "none") == 0);
    CHECK(game->entity_count == 12);
    session_tick(&s, monster->base.entity);
    qa_q1_base_monster_set_enemy(&monster->base, &player->actor.id);
    const qa_vec3 forward = qa_q1_make_vectors(game, qa_q1_get_body(game, monster->base.entity).angles).forward;
    qa_body_state player_body = qa_shared_body_table_read(game->host.bodies, player->actor.id).value;
    player_body.origin = qa_q1_vadd(qa_q1_base_monster_origin(&monster->base), qa_q1_vscale(forward, 300));
    qa_shared_body_table_write(game->host.bodies, player->actor, player_body);
    CHECK(count_class(game, "missile") == 0);
    qa_q1_base_monster_play(&monster->base, "armagon_satk1");
    for (int frame = 1; frame < 16; frame++) session_tick(&s, monster->base.entity);
    CHECK(count_class(game, "missile") == 6);
    CHECK(body->frame == monster->base.entity->frame && body->frame == 63);
    CHECK(strcmp(monster->base.current_frame, "armagon_satk16") == 0);
    qa_q1_base_monster_pain(&monster->base, &player->actor.id, 24);
    CHECK(armagon_pain_sounds == 0);
    qa_q1_base_monster_pain(&monster->base, &player->actor.id, 25);
    qa_q1_base_monster_pain(&monster->base, &player->actor.id, 100);
    CHECK(armagon_pain_sounds == 1);
    qa_gameplay_authority_set_health(game->host.combat, monster->base.entity->actor, -1);
    qa_q1_base_monster_die(&monster->base, &player->actor.id);
    int ticks = 0;
    while (strcmp(monster->base.current_frame, "armagon_die14") != 0 && ticks < 100) { session_tick(&s, monster->base.entity); ticks++; }
    CHECK(ticks == 13);
    CHECK(monster->base.entity->think.fn == NULL);
    CHECK(strcmp(monster->base.entity->movement, "none") == 0);
    CHECK(game->killed_monsters == 1);
    CHECK(count_class(game, "hip_multi_explosion") == 2);
    CHECK(qa_q1_actor_number(monster->base.entity, "gorging") == 1);
    const size_t gibs_before = count_class(game, "gib");
    for (int frame = 0; frame < 7; frame++) session_tick(&s, body);
    CHECK(qa_q1_actor_number(body, "cnt") == 7);
    CHECK(body->count == 6);
    CHECK(count_class(game, "gib") - gibs_before == 9);
    int body_ticks = 0;
    while (strcmp(body->model, "progs/s_explod.spr") != 0 && body_ticks < 100) { session_tick(&s, body); body_ticks++; }
    CHECK(body_ticks == 20);
    CHECK(body->frame == 0 && strcmp(body->movement, "none") == 0 && strcmp(body->solid, "none") == 0);
    session_close(&s);
  }
}

/* "retail Dragon keeps attack and pain phases, then resumes its saved falling death".
 * bun: dragon: yawSpeed=10 health=4000 nextFrame=dragon_activate
 *      dragon fire: shots=1 class=fireball speed=1020.3211059570312 angz=300 effects=2
 *      dragon touch: live=true
 *      dragon pain: nextFrame=dragon_painF1
 *      dragon die: groundNull=true deathState=1 killed=1
 *      dragon fall: lastVelocity=300 0 -80 len=310.4834899902344 velocity=300,0,-80 nextFrame=dragon_death3
 *      dragon land: deathState=3 nextFrame=dragon_boom2
 *      dragon end: live=false killed=1 */
static void test_dragon(void) {
  session s;
  if (!session_open(&s, "rogue", "r2m8", "rerelease", spawn_bosses, false)) { printf("skip dragon\n"); return; }
  qa_q1_services *game = s.fx.game;
  qa_q1_player_state *player = session_player(&s, 1000);
  qa_q1mpm_mission_monster *monster = find_species(&s, "dragon");
  CHECK(monster != NULL);
  if (monster == NULL) { session_close(&s); return; }
  qa_q1_actor *entity = monster->base.entity;
  session_tick(&s, entity);
  CHECK(entity->yaw_speed == 10);
  CHECK(qa_q1_health(game, entity->actor.id) == 4000);
  CHECK(strcmp(monster->base.next_frame, "dragon_activate") == 0);
  qa_q1_call_use(game, entity, entity->use, NULL, &player->actor.id);
  qa_q1_base_monster_set_enemy(&monster->base, &player->actor.id);
  (void)qa_q1_actor_field_set(entity, "dragonAttacking", "1");
  qa_body_state player_body = qa_shared_body_table_read(game->host.bodies, player->actor.id).value;
  player_body.origin = qa_q1_vadd(qa_q1_base_monster_origin(&monster->base), (qa_vec3){500, 0, 0});
  qa_shared_body_table_write(game->host.bodies, player->actor, player_body);
  qa_q1_base_monster_play(&monster->base, "dragon_atk_a2");
  qa_q1_actor *shot = NULL;
  for (size_t i = 0; i < game->entity_count && shot == NULL; i++)
    if (strcmp(game->entities[i]->classname, "fireball") == 0) shot = game->entities[i];
  CHECK(shot != NULL && count_class(game, "fireball") + count_class(game, "plasma") == 1);
  if (shot == NULL) { session_close(&s); return; }
  CHECK(qa_q1_length(qa_q1_get_body(game, shot).velocity) == 1020.3211059570312);
  CHECK(shot->angular_velocity.z == 300 && entity->effects == 2);
  qa_q1_call_touch(game, shot, shot->touch, entity->actor.id, NULL, NULL);
  CHECK(qa_q1_live(game, shot));
  s.fx.random_value = 0.1;
  (void)qa_q1_actor_field_set(entity, "dragonPainSequence", "2");
  qa_q1_base_monster_pain(&monster->base, &player->actor.id, 10);
  CHECK(strcmp(monster->base.next_frame, "dragon_painF1") == 0);
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, -1);
  qa_q1_base_monster_die(&monster->base, &player->actor.id);
  CHECK(!qa_q1_get_body(game, entity).has_ground);
  CHECK(qa_q1_actor_number(entity, "dragonDeathState") == 1 && game->killed_monsters == 1);
  session_tick(&s, entity);
  CHECK(strcmp(qa_q1_actor_field_get(entity, "dragonLastVelocity"), "300 0 -80") == 0);
  CHECK(qa_q1_length(qa_q1_actor_vector(entity, "dragonLastVelocity")) == 310.4834899902344);
  const qa_vec3 velocity = qa_q1_get_body(game, entity).velocity;
  CHECK(velocity.x == 300 && velocity.y == 0 && velocity.z == -80);
  CHECK(strcmp(monster->base.next_frame, "dragon_death3") == 0);
  qa_q1_call_touch(game, entity, entity->touch, game->world->actor.id, NULL, NULL);
  CHECK(qa_q1_actor_number(entity, "dragonDeathState") == 3 && strcmp(monster->base.next_frame, "dragon_boom2") == 0);
  session_tick(&s, entity);
  CHECK(!qa_q1_live(game, entity) && game->killed_monsters == 1);
  session_close(&s);
}

int main(void) {
  test_retail_counts();
  test_gremlin_steal();
  test_horn_charm();
  test_path_corners();
  test_armagon();
  test_dragon();
  printf("test_q1_mpm: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
