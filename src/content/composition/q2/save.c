/* Port of src/content/composition/q2/save.ts. GPL-2.0-or-later. */
#include "qa/content/composition/q2/save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/persistence/q2_base_entities.h"
#include "qa/persistence/q2_foundation.h"
#include "qa/persistence/q2_items.h"
#include "qa/persistence/q2_missionpacks.h"
#include "qa/persistence/q2_monsters.h"
#include "qa/persistence/q2_movers.h"
#include "qa/persistence/q2_players.h"
#include "qa/persistence/q2_rerelease_state.h"
#include "qa/persistence/q2_weapons.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/value.h"

static const char *pack_name(qa_q2mp_pack pack) { return pack == QA_Q2MP_PACK_XATRIX ? "xatrix" : "rogue"; }
static void *xmalloc(size_t size) {
  void *value = malloc(size != 0 ? size : 1);
  if (value == NULL) qa_range_error("out of memory");
  return value;
}
static char *copy_text(const char *text) {
  char *copy = strdup(text);
  if (copy == NULL) qa_range_error("out of memory");
  return copy;
}

/* ---- value-tree helpers (TS encodeCheckpointValue of plain objects) ---- */
static void put(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL || !qa_save_value_set(object, key, child)) qa_range_error("out of memory");
}
static qa_save_value *object_value(void) {
  qa_save_value *value = qa_save_value_object();
  if (value == NULL) qa_range_error("out of memory");
  return value;
}
static qa_save_value *saved_actor_value(qa_saved_actor_id actor) {
  qa_save_value *value = object_value();
  put(value, "slot", qa_save_value_number(actor.slot));
  put(value, "generation", qa_save_value_number(actor.generation));
  return value;
}
static qa_save_value *nullable_actor_value(bool has, qa_saved_actor_id actor) {
  return has ? saved_actor_value(actor) : qa_save_value_null();
}

/* ---- records ---- */
typedef struct records {
  qa_provider_checkpoint *items;
  size_t count, capacity;
  qa_provider_id provider;
} records;
/* TS add(schema, bytes): takes the bytes and a copy of the schema. */
static void add(records *out, const char *schema, uint8_t *bytes, size_t length) {
  if (bytes == NULL) qa_range_error("out of memory");
  if (out->count == out->capacity) {
    out->capacity = out->capacity != 0 ? out->capacity * 2 : 16;
    qa_provider_checkpoint *next = realloc(out->items, out->capacity * sizeof *next);
    if (next == NULL) qa_range_error("out of memory");
    out->items = next;
  }
  qa_provider_checkpoint record;
  memset(&record, 0, sizeof record);
  record.provider = out->provider;
  record.schema = copy_text(schema);
  record.version = 1;
  record.bytes.bytes = bytes;
  record.bytes.length = length;
  out->items[out->count++] = record;
}
static void add_value(records *out, const char *schema, qa_save_value *value) {
  size_t length = 0;
  uint8_t *bytes = qa_encode_checkpoint_value(value, &length);
  qa_save_value_free(value);
  add(out, schema, bytes, length);
}

/* The match selection as TS encodes the selection object: its own keys
 * (kind, and the DeathBall getters or LMCTF travel when present). */
static qa_save_value *selection_value(const qa_q2_match_selection *selection) {
  qa_save_value *value = object_value();
  put(value, "kind", qa_save_value_string(qa_q2_match_kind_name(selection->kind)));
  if (selection->kind == QA_Q2_MATCH_DEATHBALL) {
    char *skin1 = qa_q2_match_selection_team1_skin(selection), *skin2 = qa_q2_match_selection_team2_skin(selection);
    put(value, "team1Skin", qa_save_value_string(skin1));
    put(value, "team2Skin", qa_save_value_string(skin2));
    put(value, "goalLimit", qa_save_value_number(qa_q2_match_selection_goal_limit(selection)));
    free(skin1);
    free(skin2);
  }
  return value;
}

static qa_save_value *match_value(qa_q2_product_match *match) {
  qa_save_value *value = NULL;
  switch (match->source_kind) {
    case QA_Q2_MATCH_TAG: {
      qa_q2mp_tag_checkpoint tag = qa_q2mp_tag_capture(&match->tag);
      value = object_value();
      put(value, "token", nullable_actor_value(tag.has_token, tag.token));
      put(value, "owner", nullable_actor_value(tag.has_owner, tag.owner));
      put(value, "count", qa_save_value_number(tag.count));
      break;
    }
    case QA_Q2_MATCH_DEATHBALL: {
      qa_q2mp_deathball_checkpoint ball = qa_q2mp_deathball_capture(&match->ball);
      value = object_value();
      put(value, "ball", nullable_actor_value(ball.has_ball, ball.ball));
      put(value, "starts", qa_save_value_number(ball.starts));
      put(value, "team1Score", qa_save_value_number(ball.team1_score));
      put(value, "team2Score", qa_save_value_number(ball.team2_score));
      break;
    }
    case QA_Q2_MATCH_HORDE: {
      qa_q2_horde_checkpoint horde;
      if (!qa_q2_horde_capture(match->horde, &horde)) qa_range_error("out of memory");
      value = object_value();
      put(value, "wave", qa_save_value_number(horde.wave));
      put(value, "spawned", qa_save_value_number(horde.spawned));
      put(value, "intermission", qa_save_value_number(horde.intermission));
      qa_save_value *monsters = qa_save_value_array();
      if (monsters == NULL) qa_range_error("out of memory");
      for (size_t i = 0; i < horde.monster_count; i++)
        if (!qa_save_value_push(monsters, saved_actor_value(horde.monsters[i]))) qa_range_error("out of memory");
      put(value, "monsters", monsters);
      free(horde.monsters);
      break;
    }
    case QA_Q2_MATCH_LMCTF:
      value = qa_q2_lmctf_capture_value(match->lmctf);
      if (value == NULL) qa_range_error("out of memory");
      break;
    case QA_Q2_MATCH_CTF: case QA_Q2_MATCH_STANDARD: break;
  }
  return value;
}

qa_provider_checkpoint *qa_capture_q2_product(qa_q2_product_runtime *source, size_t *out_count) {
  qa_q2_game_services *game = source->services;
  records out;
  memset(&out, 0, sizeof out);
  out.provider = game->options.provider;
  qa_save_value *composition = object_value();
  put(composition, "edition", qa_save_value_string(source->configuration.edition == QA_Q2_EDITION_RERELEASE ? "rerelease"
                                                                                                         : "classic"));
  put(composition, "program", qa_save_value_string(qa_q2_program_name(source->configuration.program)));
  put(composition, "match", selection_value(&source->match.selection));
  put(composition, "deathmatchFlags", qa_save_value_number(qa_q2_options_deathmatch_flags(&game->options)));
  add_value(&out, "q2:composition", composition);
  size_t length = 0;
  uint8_t *bytes;

  qa_q2_foundation_checkpoint foundation = qa_q2_entity_services_capture(source->game);
  bytes = qa_encode_q2_foundation_checkpoint(&foundation, &length);
  add(&out, "q2:foundation", bytes, length);

  /* Items, movers, foundation and rerelease-module captures are scratch the
   * module owns until its next capture. */
  qa_q2_items_checkpoint items = qa_q2_item_module_capture(source->items, game);
  bytes = qa_encode_q2_items_checkpoint(&items, &length);
  add(&out, "q2:items", bytes, length);

  qa_q2_movers_checkpoint movers = qa_q2_mover_module_capture(source->movers, game);
  bytes = qa_encode_q2_movers_checkpoint(&movers, &length);
  add(&out, "q2:movers", bytes, length);

  qa_q2m_monsters_checkpoint monsters;
  if (!qa_q2m_monsters_capture(&source->monsters, &monsters)) qa_range_error("out of memory");
  bytes = qa_encode_q2_monsters_checkpoint(&monsters, &length);
  qa_q2m_monsters_checkpoint_free(&monsters);
  add(&out, "q2:monsters", bytes, length);

  qa_q2w_weapons_checkpoint weapons;
  if (!qa_q2w_weapons_capture(source->weapons, game, &weapons)) qa_range_error("out of memory");
  bytes = qa_encode_q2_weapons_checkpoint(&weapons, &length);
  qa_q2w_weapons_checkpoint_free(&weapons);
  add(&out, "q2:weapons", bytes, length);

  qa_q2_base_players_checkpoint players = qa_q2_players_capture(source->players);
  bytes = qa_encode_q2_players_checkpoint(&players, &length);
  qa_q2_base_players_checkpoint_clear(&players);
  add(&out, "q2:players", bytes, length);

  qa_q2_base_entities_module_checkpoint base = qa_q2_base_entity_module_capture(source->base_entities, game);
  bytes = qa_encode_q2_base_entities_checkpoint(&base, &length);
  qa_q2_base_entities_module_checkpoint_free(&base);
  add(&out, "q2:base-entities", bytes, length);

  if (source->armory != NULL) {
    qa_q2mp_items_checkpoint pack_items;
    if (!qa_q2mp_items_capture(&source->armory->items, game, &pack_items)) qa_range_error("out of memory");
    bytes = qa_encode_q2_mission_pack_items_checkpoint(&pack_items, &length);
    qa_q2mp_items_checkpoint_free(&pack_items);
    add(&out, "q2:missionpack-items", bytes, length);
  }
  for (size_t i = 0; i < source->expansion_count; i++) {
    qa_q2_product_expansion *expansion = &source->expansions[i];
    char schema[64];
    qa_q2mpm_monsters_checkpoint pack_monsters;
    if (!qa_q2mpm_monsters_capture(&expansion->monsters, game, &pack_monsters)) qa_range_error("out of memory");
    bytes = qa_encode_q2_mission_pack_monsters_checkpoint(&pack_monsters, &length);
    qa_q2mpm_monsters_checkpoint_free(&pack_monsters);
    snprintf(schema, sizeof schema, "q2:%s-monsters", pack_name(expansion->pack));
    add(&out, schema, bytes, length);
    qa_q2mp_rogue_entities_checkpoint entities = qa_q2mp_entities_capture(&expansion->entities);
    qa_save_value *value = object_value();
    put(value, "steamId", qa_save_value_number(entities.steam_id));
    snprintf(schema, sizeof schema, "q2:%s-entities", pack_name(expansion->pack));
    add_value(&out, schema, value);
  }
  if (source->rerelease != NULL) {
    qa_q2_rerelease_players_checkpoint rr_players = qa_q2_rerelease_players_capture(&source->rerelease->players);
    bytes = qa_encode_q2_rerelease_players_checkpoint(&rr_players, &length);
    qa_q2_rerelease_players_checkpoint_clear(&rr_players);
    add(&out, "q2:rerelease-players", bytes, length);
    qa_q2_rerelease_module_checkpoint rr_entities = qa_q2_rerelease_module_capture(&source->rerelease->entities);
    bytes = qa_encode_q2_rerelease_module_checkpoint(&rr_entities, &length);
    add(&out, "q2:rerelease-entities", bytes, length);
  }
  if (source->match.source_kind == QA_Q2_MATCH_CTF) {
    qa_q2_ctf_checkpoint ctf;
    qa_q2_ctf_capture(source->match.ctf, &ctf);
    bytes = qa_encode_q2_ctf_checkpoint(&ctf, &length);
    qa_q2_ctf_checkpoint_destroy(&ctf);
    add(&out, "q2:match", bytes, length);
  } else if (source->match.source_kind != QA_Q2_MATCH_STANDARD) {
    add_value(&out, "q2:match", match_value(&source->match));
  }
  *out_count = out.count;
  return out.items;
}

void qa_q2_product_checkpoints_free(qa_provider_checkpoint *records, size_t count) {
  for (size_t i = 0; i < count; i++) {
    free((void *)records[i].schema);
    free((void *)records[i].bytes.bytes);
  }
  free(records);
}

/* TS read(schema): exactly one record for this provider and schema, at version 1. */
typedef struct reading { const qa_provider_checkpoint *checkpoints; size_t count; qa_provider_id provider; } reading;
static const qa_provider_checkpoint *read_record(const reading *in, const char *schema) {
  const qa_provider_checkpoint *found = NULL;
  size_t matches = 0;
  for (size_t i = 0; i < in->count; i++) {
    const qa_provider_checkpoint *record = &in->checkpoints[i];
    if (strcmp(record->provider, in->provider) == 0 && strcmp(record->schema, schema) == 0) {
      if (found == NULL) found = record;
      matches++;
    }
  }
  if (found == NULL || matches != 1 || found->version != 1)
    qa_raise("Error", 0, 0, "Missing or incompatible Q2 source checkpoint %s", schema);
  return found;
}

/* A decode returns false only for bytes that are not a checkpoint value
 * (out of memory or a malformed encoding); TS decodeCheckpointValue throws. */
static void decoded(bool ok) {
  if (!ok) qa_raise("Error", 0, 0, "Malformed Q2 source checkpoint");
}
/* A decoded value tree with its root reader. */
typedef struct tree { qa_save_value *value; qa_save_reader reader; } tree;
static void tree_open(tree *t, const qa_provider_checkpoint *record, const char *path) {
  t->value = qa_decode_checkpoint_value(record->bytes.bytes, record->bytes.length);
  if (t->value == NULL || !qa_save_reader_init(&t->reader, t->value, path)) qa_range_error("out of memory");
}
static void tree_close(tree *t) {
  qa_save_reader_destroy(&t->reader);
  qa_save_value_free(t->value);
}
static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("out of memory");
  return out;
}
static const char *field_choice(const qa_save_reader *reader, const char *name, const char *const *choices, size_t count) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_save_reader_choice_string(&f, choices, count);
  qa_save_reader_destroy(&f);
  return value;
}
static double field_integer_min(const qa_save_reader *reader, const char *name, double minimum) {
  qa_save_reader f = field(reader, name);
  double value = qa_save_reader_integer_min(&f, minimum);
  qa_save_reader_destroy(&f);
  return value;
}
static double field_integer(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  double value = qa_save_reader_integer(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static double field_number(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  double value = qa_save_reader_number(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static bool field_string_is(const qa_save_reader *reader, const char *name, const char *expected) {
  qa_save_reader f = field(reader, name);
  bool same = strcmp(qa_save_reader_string(&f), expected) == 0;
  qa_save_reader_destroy(&f);
  return same;
}
/* TS reader.field(name).nullable(readSavedActor). */
static bool field_nullable_actor(const qa_save_reader *reader, const char *name, qa_saved_actor_id *out) {
  qa_save_reader f = field(reader, name);
  bool present = !qa_save_reader_is_null(&f);
  if (present) *out = qa_read_saved_actor(&f);
  qa_save_reader_destroy(&f);
  return present;
}

void qa_restore_q2_product(qa_q2_product_runtime *source, const qa_provider_checkpoint *checkpoints, size_t count) {
  qa_q2_game_services *game = source->services;
  reading in = {checkpoints, count, game->options.provider};
  static const char *const EDITIONS[] = {"classic", "rerelease"};
  static const char *const PROGRAMS[] = {"baseq2", "xatrix", "rogue", "mg2", "n64"};
  static const char *const KINDS[] = {"standard", "tag", "deathball", "ctf", "lmctf", "horde"};
  tree selection;
  tree_open(&selection, read_record(&in, "q2:composition"), "q2-composition");
  const char *edition = field_choice(&selection.reader, "edition", EDITIONS, 2);
  bool same_edition = strcmp(edition, source->configuration.edition == QA_Q2_EDITION_RERELEASE ? "rerelease" : "classic") == 0;
  if (!same_edition ||
      strcmp(field_choice(&selection.reader, "program", PROGRAMS, 5), qa_q2_program_name(source->configuration.program)) != 0)
    qa_raise("Error", 0, 0, "Q2 checkpoint belongs to a different source product");
  qa_save_reader match_selection = field(&selection.reader, "match");
  const qa_q2_match_selection *own = &source->match.selection;
  if (strcmp(field_choice(&match_selection, "kind", KINDS, 6), qa_q2_match_kind_name(own->kind)) != 0)
    qa_raise("Error", 0, 0, "Q2 checkpoint belongs to a different source match mode");
  if (own->kind == QA_Q2_MATCH_DEATHBALL) {
    char *skin1 = qa_q2_match_selection_team1_skin(own), *skin2 = qa_q2_match_selection_team2_skin(own);
    bool differ = !field_string_is(&match_selection, "team1Skin", skin1) ||
                  !field_string_is(&match_selection, "team2Skin", skin2) ||
                  field_number(&match_selection, "goalLimit") != qa_q2_match_selection_goal_limit(own);
    free(skin1);
    free(skin2);
    if (differ) qa_raise("Error", 0, 0, "Q2 checkpoint belongs to different DeathBall rules");
  }
  qa_save_reader_destroy(&match_selection);
  double deathmatch_flags = field_integer_min(&selection.reader, "deathmatchFlags", 0);
  tree_close(&selection);

  /* Every decode precedes every restore, as in TS. Each decoded checkpoint
   * borrows from its owner until the restores have copied what they keep. */
  enum { OWN_FOUNDATION, OWN_ITEMS, OWN_MOVERS, OWN_MONSTERS, OWN_WEAPONS, OWN_PLAYERS, OWN_BASE, OWN_RR_PLAYERS,
         OWN_RR_ENTITIES, OWN_COUNT };
  qa_persist_decoded owners[OWN_COUNT];
  memset(owners, 0, sizeof owners);
  const qa_provider_checkpoint *record;
  qa_q2_foundation_checkpoint foundation;
  record = read_record(&in, "q2:foundation");
  decoded(qa_decode_q2_foundation_checkpoint(record->bytes.bytes, record->bytes.length, &foundation, &owners[OWN_FOUNDATION]));
  qa_q2_items_checkpoint items;
  record = read_record(&in, "q2:items");
  decoded(qa_decode_q2_items_checkpoint(record->bytes.bytes, record->bytes.length, &items, &owners[OWN_ITEMS]));
  qa_q2_movers_checkpoint movers;
  record = read_record(&in, "q2:movers");
  decoded(qa_decode_q2_movers_checkpoint(record->bytes.bytes, record->bytes.length, &movers, &owners[OWN_MOVERS]));
  qa_q2m_monsters_checkpoint monsters;
  record = read_record(&in, "q2:monsters");
  decoded(qa_decode_q2_monsters_checkpoint(record->bytes.bytes, record->bytes.length, &monsters, &owners[OWN_MONSTERS]));
  qa_q2w_weapons_checkpoint weapons;
  record = read_record(&in, "q2:weapons");
  decoded(qa_decode_q2_weapons_checkpoint(record->bytes.bytes, record->bytes.length, &weapons, &owners[OWN_WEAPONS]));
  qa_q2_base_players_checkpoint players;
  record = read_record(&in, "q2:players");
  decoded(qa_decode_q2_players_checkpoint(record->bytes.bytes, record->bytes.length, &players, &owners[OWN_PLAYERS]));
  qa_q2_base_entities_module_checkpoint base;
  record = read_record(&in, "q2:base-entities");
  decoded(qa_decode_q2_base_entities_checkpoint(record->bytes.bytes, record->bytes.length, &base, &owners[OWN_BASE]));
  qa_q2mp_items_checkpoint pack_items;
  if (source->armory != NULL) {
    record = read_record(&in, "q2:missionpack-items");
    decoded(qa_decode_q2_mission_pack_items_checkpoint(record->bytes.bytes, record->bytes.length, &pack_items));
  }
  qa_q2mpm_monsters_checkpoint pack_monsters[QA_Q2_PRODUCT_MAX_EXPANSIONS];
  qa_q2mp_rogue_entities_checkpoint pack_entities[QA_Q2_PRODUCT_MAX_EXPANSIONS];
  for (size_t i = 0; i < source->expansion_count; i++) {
    char schema[64];
    snprintf(schema, sizeof schema, "q2:%s-monsters", pack_name(source->expansions[i].pack));
    record = read_record(&in, schema);
    decoded(qa_decode_q2_mission_pack_monsters_checkpoint(record->bytes.bytes, record->bytes.length, &pack_monsters[i]));
    snprintf(schema, sizeof schema, "q2:%s-entities", pack_name(source->expansions[i].pack));
    tree entities;
    tree_open(&entities, read_record(&in, schema), "q2-missionpack-entities");
    pack_entities[i].steam_id = field_integer(&entities.reader, "steamId");
    tree_close(&entities);
  }
  qa_q2_rerelease_players_checkpoint rr_players;
  qa_q2_rerelease_module_checkpoint rr_entities;
  if (source->rerelease != NULL) {
    record = read_record(&in, "q2:rerelease-players");
    decoded(qa_decode_q2_rerelease_players_checkpoint(record->bytes.bytes, record->bytes.length, &rr_players,
                                                      &owners[OWN_RR_PLAYERS]));
    record = read_record(&in, "q2:rerelease-entities");
    decoded(qa_decode_q2_rerelease_module_checkpoint(record->bytes.bytes, record->bytes.length, &rr_entities,
                                                     &owners[OWN_RR_ENTITIES]));
  }
  const qa_provider_checkpoint *match_record = source->match.source_kind == QA_Q2_MATCH_STANDARD ? NULL
                                                                                                  : read_record(&in, "q2:match");
  tree match;
  bool has_match_tree = match_record != NULL;
  if (has_match_tree) tree_open(&match, match_record, "q2-match");
  qa_q2mp_tag_checkpoint tag;
  qa_q2mp_deathball_checkpoint ball;
  qa_q2_horde_checkpoint horde;
  memset(&horde, 0, sizeof horde);
  if (source->match.source_kind == QA_Q2_MATCH_TAG) {
    tag.has_token = field_nullable_actor(&match.reader, "token", &tag.token);
    tag.has_owner = field_nullable_actor(&match.reader, "owner", &tag.owner);
    tag.count = field_integer_min(&match.reader, "count", 0);
  } else if (source->match.source_kind == QA_Q2_MATCH_DEATHBALL) {
    ball.has_ball = field_nullable_actor(&match.reader, "ball", &ball.ball);
    ball.starts = field_integer_min(&match.reader, "starts", 0);
    ball.team1_score = field_integer(&match.reader, "team1Score");
    ball.team2_score = field_integer(&match.reader, "team2Score");
  } else if (source->match.source_kind == QA_Q2_MATCH_HORDE) {
    horde.wave = field_integer_min(&match.reader, "wave", 0);
    horde.spawned = field_integer_min(&match.reader, "spawned", 0);
    horde.intermission = field_integer_min(&match.reader, "intermission", 0);
    qa_save_reader list = field(&match.reader, "monsters");
    horde.monster_count = qa_save_reader_list_count(&list);
    horde.monsters = xmalloc(horde.monster_count * sizeof *horde.monsters);
    for (size_t i = 0; i < horde.monster_count; i++) {
      qa_save_reader item;
      if (!qa_save_reader_item(&list, i, &item)) qa_range_error("out of memory");
      horde.monsters[i] = qa_read_saved_actor(&item);
      qa_save_reader_destroy(&item);
    }
    qa_save_reader_destroy(&list);
  }

  qa_q2_product_runtime_register_callbacks(source);
  qa_q2_product_runtime_set_deathmatch_flags(source, deathmatch_flags);
  qa_q2_entity_services_restore(source->game, foundation);
  qa_q2_item_module_restore(source->items, game, items);
  qa_q2_mover_module_restore(source->movers, game, movers);
  if (source->armory != NULL) qa_q2mp_items_restore(&source->armory->items, game, &pack_items);
  for (size_t i = 0; i < source->expansion_count; i++) {
    qa_q2mp_entities_restore(&source->expansions[i].entities, pack_entities[i]);
    qa_q2mpm_monsters_restore(&source->expansions[i].monsters, game, &pack_monsters[i]);
  }
  qa_q2w_weapons_restore(source->weapons, game, &weapons);
  qa_q2m_monsters_restore(&source->monsters, game, &monsters);
  qa_q2_base_entity_module_restore(source->base_entities, game, base);
  qa_q2_players_restore(source->players, game, players);
  if (source->rerelease != NULL) {
    qa_q2_rerelease_players_restore(&source->rerelease->players, game, &rr_players);
    qa_q2_rerelease_module_restore(&source->rerelease->entities, game, rr_entities);
  }
  if (source->match.source_kind == QA_Q2_MATCH_CTF) {
    qa_q2_ctf_checkpoint ctf;
    qa_decode_q2_ctf_checkpoint(match_record->bytes.bytes, match_record->bytes.length, &ctf);
    qa_q2_ctf_restore(source->match.ctf, &ctf, game);
    qa_q2_ctf_checkpoint_destroy(&ctf);
  }
  if (source->match.source_kind == QA_Q2_MATCH_LMCTF) qa_q2_lmctf_restore(source->match.lmctf, &match.reader, game);
  if (source->match.source_kind == QA_Q2_MATCH_TAG) qa_q2mp_tag_restore(&source->match.tag, game, tag);
  if (source->match.source_kind == QA_Q2_MATCH_DEATHBALL) qa_q2mp_deathball_restore(&source->match.ball, game, ball);
  if (source->match.source_kind == QA_Q2_MATCH_HORDE) qa_q2_horde_restore(source->match.horde, game, horde);
  free(horde.monsters);
  if (has_match_tree) tree_close(&match);
  /* Release the decoded checkpoints (TS GC). */
  for (size_t i = 0; i < OWN_COUNT; i++) qa_persist_decoded_free(&owners[i]);
  if (source->armory != NULL) qa_q2mp_items_checkpoint_free(&pack_items);
  for (size_t i = 0; i < source->expansion_count; i++) qa_q2mpm_monsters_checkpoint_free(&pack_monsters[i]);
}
