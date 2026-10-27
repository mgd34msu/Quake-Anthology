/* Port of src/content/composition/q2/save.ts. GPL-2.0-or-later. */
#include "qa/content/composition/q2/save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static bool provider_eq(qa_provider_id left, qa_provider_id right) {
  if (left == NULL || right == NULL) return left == right;
  return strcmp(left, right) == 0;
}

static bool add_record(qa_provider_checkpoint *out, size_t capacity, size_t *count, qa_provider_id provider,
                       const char *schema, uint8_t *bytes, size_t length) {
  if (bytes == NULL || *count >= capacity) return false;
  out[*count] = (qa_provider_checkpoint){provider, schema, 1, {bytes, length}};
  (*count)++;
  return true;
}

static bool set_str(qa_save_value *object, const char *key, const char *value) {
  qa_save_value *child = qa_save_value_string(value);
  return child != NULL && qa_save_value_set(object, key, child);
}
static bool set_num(qa_save_value *object, const char *key, double value) {
  qa_save_value *child = qa_save_value_number(value);
  return child != NULL && qa_save_value_set(object, key, child);
}

/* The match selection value: {kind, + deathball skins/goal, + lmctf travel
 * presence}. The TS encodes the selection object itself (getters read);
 * restore only reads kind and the deathball fields. */
static qa_save_value *match_selection_value(const qa_comp_q2_match_selection *selection) {
  qa_save_value *value = qa_save_value_object();
  if (value == NULL) return NULL;
  bool ok = set_str(value, "kind", qa_comp_q2_match_kind_name(selection->kind));
  if (ok && selection->kind == QA_COMP_Q2_MATCH_DEATHBALL) {
    bool owned1 = false, owned2 = false;
    const char *skin1 = qa_comp_q2_match_selection_team1_skin(selection, &owned1);
    const char *skin2 = qa_comp_q2_match_selection_team2_skin(selection, &owned2);
    ok = set_str(value, "team1Skin", skin1 != NULL ? skin1 : "") && set_str(value, "team2Skin", skin2 != NULL ? skin2 : "") &&
         set_num(value, "goalLimit", qa_comp_q2_match_selection_goal_limit(selection));
    if (owned1) free((void *)skin1);
    if (owned2) free((void *)skin2);
  }
  if (!ok) { qa_save_value_free(value); return NULL; }
  return value;
}

size_t qa_comp_capture_q2_product(qa_comp_q2_product_runtime *source, const qa_comp_q2_save_hooks *hooks,
                                  qa_provider_checkpoint *out, size_t capacity) {
  size_t count = 0;
  qa_provider_id provider = hooks->provider;
  /* q2:composition */
  qa_save_value *composition = qa_save_value_object();
  qa_save_value *match = match_selection_value(&source->match.selection);
  bool ok = composition != NULL && match != NULL;
  if (ok) ok = set_str(composition, "edition", source->configuration.edition) &&
               set_str(composition, "program", qa_comp_q2_program_name(source->configuration.program)) &&
               qa_save_value_set(composition, "match", match) &&
               set_num(composition, "deathmatchFlags", hooks->deathmatch_flags(hooks->ctx, source));
  size_t length = 0;
  uint8_t *bytes = ok ? qa_encode_checkpoint_value(composition, &length) : NULL;
  if (!ok) qa_save_value_free(match);
  qa_save_value_free(composition);
  if (!add_record(out, capacity, &count, provider, "q2:composition", bytes, length)) goto oom;
  static const struct { qa_comp_q2_save_module module; const char *schema; } BASE[] = {
      {QA_COMP_Q2_SAVE_FOUNDATION, "q2:foundation"}, {QA_COMP_Q2_SAVE_ITEMS, "q2:items"},
      {QA_COMP_Q2_SAVE_MOVERS, "q2:movers"}, {QA_COMP_Q2_SAVE_MONSTERS, "q2:monsters"},
      {QA_COMP_Q2_SAVE_WEAPONS, "q2:weapons"}, {QA_COMP_Q2_SAVE_PLAYERS, "q2:players"},
      {QA_COMP_Q2_SAVE_BASE_ENTITIES, "q2:base-entities"}};
  for (size_t i = 0; i < sizeof BASE / sizeof *BASE; i++) {
    bytes = hooks->capture_module(hooks->ctx, source, BASE[i].module, NULL, &length);
    if (!add_record(out, capacity, &count, provider, BASE[i].schema, bytes, length)) goto oom;
  }
  if (source->armory != NULL) {
    bytes = hooks->capture_module(hooks->ctx, source, QA_COMP_Q2_SAVE_MISSIONPACK_ITEMS, NULL, &length);
    if (!add_record(out, capacity, &count, provider, "q2:missionpack-items", bytes, length)) goto oom;
  }
  for (size_t i = 0; i < source->expansion_count; i++) {
    const char *pack = source->expansions[i].pack;
    char schema[64];
    bytes = hooks->capture_module(hooks->ctx, source, QA_COMP_Q2_SAVE_PACK_MONSTERS, pack, &length);
    (void)snprintf(schema, sizeof schema, "q2:%s-monsters", pack);
    /* schema text must outlive the call: records borrow it like TS strings;
     * duplicate it onto the heap via the bytes-owner convention below. */
    char *schema_copy = (char *)malloc(strlen(schema) + 1);
    if (schema_copy == NULL || bytes == NULL) { free(schema_copy); free(bytes); goto oom; }
    strcpy(schema_copy, schema);
    if (!add_record(out, capacity, &count, provider, schema_copy, bytes, length)) { free(schema_copy); goto oom; }
    qa_save_value *entities = qa_save_value_object();
    ok = entities != NULL &&
         set_num(entities, "steamId",
                 source->hooks->pack_entities_capture_steam_id(source->hooks->ctx, source->expansions[i].entities));
    bytes = ok ? qa_encode_checkpoint_value(entities, &length) : NULL;
    qa_save_value_free(entities);
    (void)snprintf(schema, sizeof schema, "q2:%s-entities", pack);
    schema_copy = (char *)malloc(strlen(schema) + 1);
    if (schema_copy == NULL || bytes == NULL) { free(schema_copy); free(bytes); goto oom; }
    strcpy(schema_copy, schema);
    if (!add_record(out, capacity, &count, provider, schema_copy, bytes, length)) { free(schema_copy); goto oom; }
  }
  if (source->has_rerelease) {
    bytes = hooks->capture_module(hooks->ctx, source, QA_COMP_Q2_SAVE_RERELEASE_PLAYERS, NULL, &length);
    if (!add_record(out, capacity, &count, provider, "q2:rerelease-players", bytes, length)) goto oom;
    bytes = hooks->capture_module(hooks->ctx, source, QA_COMP_Q2_SAVE_RERELEASE_ENTITIES, NULL, &length);
    if (!add_record(out, capacity, &count, provider, "q2:rerelease-entities", bytes, length)) goto oom;
  }
  if (source->match.source != NULL) {
    qa_save_value *value = hooks->capture_match(hooks->ctx, source);
    bytes = value != NULL ? qa_encode_checkpoint_value(value, &length) : NULL;
    qa_save_value_free(value);
    if (!add_record(out, capacity, &count, provider, "q2:match", bytes, length)) goto oom;
  }
  return count;
oom:
  qa_comp_q2_save_free_records(out, count);
  return 0;
}

void qa_comp_q2_save_free_records(qa_provider_checkpoint *records, size_t count) {
  for (size_t i = 0; i < count; i++) {
    free((void *)records[i].bytes.bytes);
    /* Pack schemas were heap-copied above; static schemas are literals.
     * Distinguish by the q2:<pack>- prefix forms we allocated. */
    if (records[i].schema != NULL && strncmp(records[i].schema, "q2:", 3) == 0 &&
        (strstr(records[i].schema, "-monsters") != NULL || strstr(records[i].schema, "-entities") != NULL) &&
        strcmp(records[i].schema, "q2:monsters") != 0 && strcmp(records[i].schema, "q2:base-entities") != 0 &&
        strcmp(records[i].schema, "q2:rerelease-entities") != 0)
      free((void *)records[i].schema);
  }
}

static const qa_provider_checkpoint *read_record(const qa_provider_checkpoint *checkpoints, size_t count,
                                                 qa_provider_id provider, const char *schema) {
  const qa_provider_checkpoint *found = NULL;
  size_t matches = 0;
  for (size_t i = 0; i < count; i++)
    if (provider_eq(checkpoints[i].provider, provider) && strcmp(checkpoints[i].schema, schema) == 0) {
      found = &checkpoints[i];
      matches++;
    }
  if (found == NULL || matches != 1 || found->version != 1)
    qa_range_error("Missing or incompatible Q2 source checkpoint %s", schema);
  return found;
}

static void *decode(const qa_comp_q2_save_hooks *hooks, qa_comp_q2_save_module module, const char *pack,
                    const qa_provider_checkpoint *record) {
  return hooks->decode_module(hooks->ctx, module, pack, record->bytes.bytes, record->bytes.length);
}

static const char *reader_choice(qa_save_reader *reader, const char *name, const char *const *choices, size_t count) {
  qa_save_reader field;
  if (!qa_save_reader_field(reader, name, &field)) qa_range_error("Out of memory");
  const char *value = qa_save_reader_choice_string(&field, choices, count);
  qa_save_reader_destroy(&field);
  return value;
}
static double reader_integer0(qa_save_reader *reader, const char *name) {
  qa_save_reader field;
  if (!qa_save_reader_field(reader, name, &field)) qa_range_error("Out of memory");
  double value = qa_save_reader_integer_min(&field, 0);
  qa_save_reader_destroy(&field);
  return value;
}
static bool reader_nullable_actor(qa_save_reader *reader, const char *name, qa_saved_actor_id *out) {
  qa_save_reader field;
  if (!qa_save_reader_field(reader, name, &field)) qa_range_error("Out of memory");
  bool present = !qa_save_reader_is_null(&field);
  if (present) *out = qa_read_saved_actor(&field);
  qa_save_reader_destroy(&field);
  return present;
}

bool qa_comp_restore_q2_product(qa_comp_q2_product_runtime *source, const qa_comp_q2_save_hooks *hooks,
                                const qa_provider_checkpoint *checkpoints, size_t checkpoint_count) {
  qa_provider_id provider = hooks->provider;
#define READ(schema) read_record(checkpoints, checkpoint_count, provider, schema)
  const qa_provider_checkpoint *composition_record = READ("q2:composition");
  qa_save_value *composition_value = qa_decode_checkpoint_value(composition_record->bytes.bytes, composition_record->bytes.length);
  if (composition_value == NULL) return false;
  qa_save_reader selection;
  if (!qa_save_reader_init(&selection, composition_value, "q2-composition")) { qa_save_value_free(composition_value); return false; }
  static const char *const EDITIONS[] = {"classic", "rerelease"};
  static const char *const PROGRAMS[] = {"baseq2", "xatrix", "rogue", "mg2", "n64"};
  static const char *const KINDS[] = {"standard", "tag", "deathball", "ctf", "lmctf", "horde"};
  if (strcmp(reader_choice(&selection, "edition", EDITIONS, 2), source->configuration.edition) != 0 ||
      strcmp(reader_choice(&selection, "program", PROGRAMS, 5), qa_comp_q2_program_name(source->configuration.program)) != 0)
    qa_range_error("Q2 checkpoint belongs to a different source product");
  qa_save_reader match_selection;
  if (!qa_save_reader_field(&selection, "match", &match_selection)) qa_range_error("Out of memory");
  if (strcmp(reader_choice(&match_selection, "kind", KINDS, 6), qa_comp_q2_match_kind_name(source->match.selection.kind)) != 0)
    qa_range_error("Q2 checkpoint belongs to a different source match mode");
  if (source->match.selection.kind == QA_COMP_Q2_MATCH_DEATHBALL) {
    qa_save_reader field;
    bool owned1 = false, owned2 = false;
    const char *skin1 = qa_comp_q2_match_selection_team1_skin(&source->match.selection, &owned1);
    const char *skin2 = qa_comp_q2_match_selection_team2_skin(&source->match.selection, &owned2);
    bool differ = false;
    double goal = 0;
    const char *saved1 = NULL, *saved2 = NULL;
    if (!qa_save_reader_field(&match_selection, "team1Skin", &field)) qa_range_error("Out of memory");
    saved1 = qa_save_reader_string(&field);
    qa_save_reader_destroy(&field);
    if (!qa_save_reader_field(&match_selection, "team2Skin", &field)) qa_range_error("Out of memory");
    saved2 = qa_save_reader_string(&field);
    qa_save_reader_destroy(&field);
    if (!qa_save_reader_field(&match_selection, "goalLimit", &field)) qa_range_error("Out of memory");
    goal = qa_save_reader_number(&field);
    qa_save_reader_destroy(&field);
    differ = strcmp(saved1, skin1 != NULL ? skin1 : "") != 0 || strcmp(saved2, skin2 != NULL ? skin2 : "") != 0 ||
             goal != qa_comp_q2_match_selection_goal_limit(&source->match.selection);
    if (owned1) free((void *)skin1);
    if (owned2) free((void *)skin2);
    if (differ) qa_range_error("Q2 checkpoint belongs to different DeathBall rules");
  }
  double deathmatch_flags = reader_integer0(&selection, "deathmatchFlags");
  qa_save_reader_destroy(&match_selection);
  qa_save_reader_destroy(&selection);
  qa_save_value_free(composition_value);

  /* Phase 1: decode everything (TS order). */
  void *foundation = decode(hooks, QA_COMP_Q2_SAVE_FOUNDATION, NULL, READ("q2:foundation"));
  void *items = decode(hooks, QA_COMP_Q2_SAVE_ITEMS, NULL, READ("q2:items"));
  void *movers = decode(hooks, QA_COMP_Q2_SAVE_MOVERS, NULL, READ("q2:movers"));
  void *monsters = decode(hooks, QA_COMP_Q2_SAVE_MONSTERS, NULL, READ("q2:monsters"));
  void *weapons = decode(hooks, QA_COMP_Q2_SAVE_WEAPONS, NULL, READ("q2:weapons"));
  void *players = decode(hooks, QA_COMP_Q2_SAVE_PLAYERS, NULL, READ("q2:players"));
  void *base_entities = decode(hooks, QA_COMP_Q2_SAVE_BASE_ENTITIES, NULL, READ("q2:base-entities"));
  void *armory = source->armory == NULL ? NULL : decode(hooks, QA_COMP_Q2_SAVE_MISSIONPACK_ITEMS, NULL, READ("q2:missionpack-items"));
  void *pack_monsters[QA_COMP_Q2_MAX_EXPANSIONS] = {NULL, NULL};
  double steam_ids[QA_COMP_Q2_MAX_EXPANSIONS] = {0, 0};
  for (size_t i = 0; i < source->expansion_count; i++) {
    char schema[64];
    (void)snprintf(schema, sizeof schema, "q2:%s-monsters", source->expansions[i].pack);
    pack_monsters[i] = decode(hooks, QA_COMP_Q2_SAVE_PACK_MONSTERS, source->expansions[i].pack, READ(schema));
    (void)snprintf(schema, sizeof schema, "q2:%s-entities", source->expansions[i].pack);
    const qa_provider_checkpoint *record = READ(schema);
    qa_save_value *value = qa_decode_checkpoint_value(record->bytes.bytes, record->bytes.length);
    if (value == NULL) return false;
    qa_save_reader reader, field;
    if (!qa_save_reader_init(&reader, value, "q2-missionpack-entities") ||
        !qa_save_reader_field(&reader, "steamId", &field))
      qa_range_error("Out of memory");
    steam_ids[i] = qa_save_reader_integer(&field);
    qa_save_reader_destroy(&field);
    qa_save_reader_destroy(&reader);
    qa_save_value_free(value);
  }
  void *rerelease_players = NULL, *rerelease_entities = NULL;
  if (source->has_rerelease) {
    rerelease_players = decode(hooks, QA_COMP_Q2_SAVE_RERELEASE_PLAYERS, NULL, READ("q2:rerelease-players"));
    rerelease_entities = decode(hooks, QA_COMP_Q2_SAVE_RERELEASE_ENTITIES, NULL, READ("q2:rerelease-entities"));
  }
  const qa_provider_checkpoint *match_record = source->match.source == NULL ? NULL : READ("q2:match");
  qa_save_value *match_value = NULL;
  qa_comp_q2_tag_state tag = {0};
  qa_comp_q2_ball_state ball = {0};
  qa_comp_q2_horde_state horde = {0};
  qa_saved_actor_id *horde_monsters = NULL;
  bool has_tag = false, has_ball = false, has_horde = false;
  qa_save_reader match_reader;
  bool has_match_reader = false;
  if (match_record != NULL && source->match.source_kind != QA_COMP_Q2_MATCH_CTF) {
    match_value = qa_decode_checkpoint_value(match_record->bytes.bytes, match_record->bytes.length);
    if (match_value == NULL) return false;
    if (!qa_save_reader_init(&match_reader, match_value, "q2-match")) { qa_save_value_free(match_value); return false; }
    has_match_reader = true;
    if (source->match.source_kind == QA_COMP_Q2_MATCH_TAG) {
      has_tag = true;
      tag.has_token = reader_nullable_actor(&match_reader, "token", &tag.token);
      tag.has_owner = reader_nullable_actor(&match_reader, "owner", &tag.owner);
      tag.count = reader_integer0(&match_reader, "count");
    } else if (source->match.source_kind == QA_COMP_Q2_MATCH_DEATHBALL) {
      has_ball = true;
      ball.has_ball = reader_nullable_actor(&match_reader, "ball", &ball.ball);
      ball.starts = reader_integer0(&match_reader, "starts");
      qa_save_reader field;
      if (!qa_save_reader_field(&match_reader, "team1Score", &field)) qa_range_error("Out of memory");
      ball.team1_score = qa_save_reader_integer(&field);
      qa_save_reader_destroy(&field);
      if (!qa_save_reader_field(&match_reader, "team2Score", &field)) qa_range_error("Out of memory");
      ball.team2_score = qa_save_reader_integer(&field);
      qa_save_reader_destroy(&field);
    } else if (source->match.source_kind == QA_COMP_Q2_MATCH_HORDE) {
      has_horde = true;
      horde.wave = reader_integer0(&match_reader, "wave");
      horde.spawned = reader_integer0(&match_reader, "spawned");
      horde.intermission = reader_integer0(&match_reader, "intermission");
      qa_save_reader field;
      if (!qa_save_reader_field(&match_reader, "monsters", &field)) qa_range_error("Out of memory");
      size_t count = qa_save_reader_list_count(&field);
      horde_monsters = (qa_saved_actor_id *)malloc((count > 0 ? count : 1) * sizeof *horde_monsters);
      if (horde_monsters == NULL) { qa_save_reader_destroy(&field); return false; }
      for (size_t i = 0; i < count; i++) {
        qa_save_reader item;
        if (!qa_save_reader_item(&field, i, &item)) qa_range_error("Out of memory");
        horde_monsters[i] = qa_read_saved_actor(&item);
        qa_save_reader_destroy(&item);
      }
      horde.monsters = horde_monsters;
      horde.monster_count = count;
      qa_save_reader_destroy(&field);
    }
  }
  if (foundation == NULL || items == NULL || movers == NULL || monsters == NULL || weapons == NULL ||
      players == NULL || base_entities == NULL || (source->armory != NULL && armory == NULL))
    return false;

  /* Phase 2: restores, in TS order. */
  qa_comp_q2_product_runtime_register_callbacks(source);
  qa_comp_q2_product_runtime_set_deathmatch_flags(source, deathmatch_flags);
  hooks->restore_module(hooks->ctx, source, QA_COMP_Q2_SAVE_FOUNDATION, NULL, foundation);
  hooks->restore_module(hooks->ctx, source, QA_COMP_Q2_SAVE_ITEMS, NULL, items);
  hooks->restore_module(hooks->ctx, source, QA_COMP_Q2_SAVE_MOVERS, NULL, movers);
  if (armory != NULL) hooks->restore_module(hooks->ctx, source, QA_COMP_Q2_SAVE_MISSIONPACK_ITEMS, NULL, armory);
  for (size_t i = 0; i < source->expansion_count; i++) {
    source->hooks->pack_entities_restore(source->hooks->ctx, source->expansions[i].entities, steam_ids[i]);
    hooks->restore_module(hooks->ctx, source, QA_COMP_Q2_SAVE_PACK_MONSTERS, source->expansions[i].pack, pack_monsters[i]);
  }
  hooks->restore_module(hooks->ctx, source, QA_COMP_Q2_SAVE_WEAPONS, NULL, weapons);
  hooks->restore_module(hooks->ctx, source, QA_COMP_Q2_SAVE_MONSTERS, NULL, monsters);
  hooks->restore_module(hooks->ctx, source, QA_COMP_Q2_SAVE_BASE_ENTITIES, NULL, base_entities);
  hooks->restore_module(hooks->ctx, source, QA_COMP_Q2_SAVE_PLAYERS, NULL, players);
  if (source->has_rerelease) {
    hooks->restore_module(hooks->ctx, source, QA_COMP_Q2_SAVE_RERELEASE_PLAYERS, NULL, rerelease_players);
    hooks->restore_module(hooks->ctx, source, QA_COMP_Q2_SAVE_RERELEASE_ENTITIES, NULL, rerelease_entities);
  }
  if (match_record != NULL && source->match.source_kind == QA_COMP_Q2_MATCH_CTF)
    hooks->restore_ctf(hooks->ctx, source, match_record->bytes.bytes, match_record->bytes.length);
  if (has_match_reader && source->match.source_kind == QA_COMP_Q2_MATCH_LMCTF)
    hooks->restore_lmctf(hooks->ctx, source, &match_reader);
  if (has_tag) hooks->restore_tag(hooks->ctx, source, &tag);
  if (has_ball) hooks->restore_ball(hooks->ctx, source, &ball);
  if (has_horde) hooks->restore_horde(hooks->ctx, source, &horde);
  void *decoded[] = {foundation, items, movers, monsters, weapons, players, base_entities, armory,
                     pack_monsters[0], pack_monsters[1], rerelease_players, rerelease_entities};
  for (size_t i = 0; i < sizeof decoded / sizeof *decoded; i++)
    if (decoded[i] != NULL) hooks->free_decoded(hooks->ctx, decoded[i]);
  if (has_match_reader) qa_save_reader_destroy(&match_reader);
  qa_save_value_free(match_value);
  free(horde_monsters);
  return true;
#undef READ
}
