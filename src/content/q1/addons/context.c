/* Port of src/content/q1/addons/context.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/context.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/campaign.h"
#include "qa/content/q1/addons/items/index.h"
#include "qa/content/q1/addons/monsters/index.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/persistence/value.h"

const char *qa_q1ad_program_name(qa_q1ad_program program) {
  static const char *const NAMES[] = {"dopa", "mg1", "mg3", "ctf"};
  return NAMES[program];
}

static char *dup_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy != NULL) memcpy(copy, text, length);
  return copy;
}

void qa_q1ad_set_text(char **slot, const char *text) {
  if (*slot == text) return;
  char *copy = dup_text(text);
  if (copy == NULL) return;
  free(*slot);
  *slot = copy;
}

/* JS String(Math.fround(value)). */
static void format_number(char *out, size_t size, double value) {
  qa_net_js_number((double)(float)value, out, size);
}

static bool same_owned(qa_owned_actor a, qa_owned_actor b) { return qa_same_actor(a.id, b.id); }

static bool resolve_owned(qa_q1ad_context *context, qa_actor_id actor, qa_owned_actor *out) {
  return qa_session_actor_registry_resolve_owned(context->game->host.actors, actor, out);
}

/* --- playerWords / playerReferences / frameTicks storage --- */
static bool grow(void **items, size_t *capacity, size_t count, size_t size, size_t initial) {
  if (count < *capacity) return true;
  size_t next_capacity = *capacity == 0 ? initial : *capacity * 2;
  void *next = realloc(*items, next_capacity * size);
  if (next == NULL) return false;
  *items = next;
  *capacity = next_capacity;
  return true;
}

/* playerWords.set(actor, words): admits the owner even with no words; a
 * NULL-name entry holds its Map position until words follow it. */
static void mark_player(qa_q1ad_context *context, qa_owned_actor owner) {
  for (size_t i = 0; i < context->word_count; i++)
    if (same_owned(context->words[i].owner, owner)) return;
  if (!grow((void **)&context->words, &context->word_capacity, context->word_count, sizeof *context->words, 32)) return;
  context->words[context->word_count++] = (struct qa_q1ad_word){.owner = owner, .name = NULL, .value = 0};
}

static void reset_owner_words(qa_q1ad_context *context, qa_owned_actor owner) {
  size_t kept = 0;
  bool placed = false;
  for (size_t i = 0; i < context->word_count; i++) {
    if (!same_owned(context->words[i].owner, owner)) { context->words[kept++] = context->words[i]; continue; }
    free(context->words[i].name);
    if (!placed) { context->words[kept++] = (struct qa_q1ad_word){.owner = owner}; placed = true; }
  }
  context->word_count = kept;
  if (!placed) mark_player(context, owner);
}

static void put_word(qa_q1ad_context *context, qa_owned_actor owner, const char *name, double value) {
  size_t last = context->word_count;
  for (size_t i = 0; i < context->word_count; i++) {
    if (!same_owned(context->words[i].owner, owner)) continue;
    if (context->words[i].name == NULL) { /* placeholder becomes the first word */
      char *copy = dup_text(name);
      if (copy == NULL) return;
      context->words[i].name = copy;
      context->words[i].value = value;
      return;
    }
    if (strcmp(context->words[i].name, name) == 0) { context->words[i].value = value; return; }
    last = i + 1;
  }
  if (!grow((void **)&context->words, &context->word_capacity, context->word_count, sizeof *context->words, 32)) return;
  char *copy = dup_text(name);
  if (copy == NULL) return;
  /* Keep each owner's words contiguous after its first entry (Map order). */
  memmove(&context->words[last + 1], &context->words[last], (context->word_count - last) * sizeof *context->words);
  context->words[last] = (struct qa_q1ad_word){.owner = owner, .name = copy, .value = value};
  context->word_count++;
}

static void put_ref(qa_q1ad_context *context, qa_owned_actor owner, const char *name, const qa_actor_id *target) {
  size_t last = context->ref_count;
  for (size_t i = 0; i < context->ref_count; i++) {
    if (!same_owned(context->refs[i].owner, owner)) continue;
    if (strcmp(context->refs[i].name, name) == 0) {
      context->refs[i].has_target = target != NULL;
      if (target != NULL) context->refs[i].target = *target;
      return;
    }
    last = i + 1;
  }
  if (!grow((void **)&context->refs, &context->ref_capacity, context->ref_count, sizeof *context->refs, 16)) return;
  char *copy = dup_text(name);
  if (copy == NULL) return;
  memmove(&context->refs[last + 1], &context->refs[last], (context->ref_count - last) * sizeof *context->refs);
  context->refs[last] = (struct qa_q1ad_ref){.owner = owner, .name = copy, .has_target = target != NULL};
  if (target != NULL) context->refs[last].target = *target;
  context->ref_count++;
}

static void clear_words(qa_q1ad_context *context) {
  for (size_t i = 0; i < context->word_count; i++) free(context->words[i].name);
  context->word_count = 0;
}
static void clear_refs(qa_q1ad_context *context) {
  for (size_t i = 0; i < context->ref_count; i++) free(context->refs[i].name);
  context->ref_count = 0;
}

/* host.actors.onRelease handler. */
static void on_release(void *ctx, qa_owned_actor actor) {
  qa_q1ad_context *context = ctx;
  /* Map.delete keeps the remaining owners in insertion order. */
  size_t kept = 0;
  for (size_t i = 0; i < context->word_count; i++)
    if (same_owned(context->words[i].owner, actor)) free(context->words[i].name);
    else context->words[kept++] = context->words[i];
  context->word_count = kept;
  kept = 0;
  for (size_t i = 0; i < context->ref_count; i++)
    if (same_owned(context->refs[i].owner, actor)) free(context->refs[i].name);
    else context->refs[kept++] = context->refs[i];
  context->ref_count = kept;
  for (size_t i = 0; i < context->tick_count; i++)
    if (qa_same_actor(context->ticks[i]->actor.id, actor.id)) {
      memmove(&context->ticks[i], &context->ticks[i + 1], (context->tick_count - i - 1) * sizeof *context->ticks);
      context->tick_count--;
      break;
    }
}

/* --- checkpoint extension --- */
static qa_save_value *actor_ref_value(int64_t slot, int64_t generation) {
  qa_save_value *object = qa_save_value_object();
  qa_save_value *s = qa_save_value_number((double)slot);
  qa_save_value *g = qa_save_value_number((double)generation);
  if (object == NULL || s == NULL || g == NULL) {
    qa_save_value_free(object); qa_save_value_free(s); qa_save_value_free(g);
    return NULL;
  }
  /* qa_save_value_set takes the child only on success. */
  if (!qa_save_value_set(object, "slot", s)) { qa_save_value_free(object); qa_save_value_free(s); qa_save_value_free(g); return NULL; }
  if (!qa_save_value_set(object, "generation", g)) { qa_save_value_free(object); qa_save_value_free(g); return NULL; }
  return object;
}

static bool capture(void *ctx, uint8_t **out, size_t *out_len) {
  qa_q1ad_context *context = ctx;
  *out = NULL;
  *out_len = 0;
  qa_save_value *root = qa_save_value_object();
  qa_save_value *players = qa_save_value_array();
  qa_save_value *references = qa_save_value_array();
  qa_save_value *ticks = qa_save_value_array();
  if (root == NULL || players == NULL || references == NULL || ticks == NULL) goto oom;
  for (size_t i = 0; i < context->word_count; i++) {
    if (i > 0 && same_owned(context->words[i - 1].owner, context->words[i].owner)) continue;
    qa_save_value *entry = actor_ref_value(context->words[i].owner.id.slot, context->words[i].owner.id.generation);
    qa_save_value *words = qa_save_value_array();
    if (entry == NULL || words == NULL) { qa_save_value_free(entry); qa_save_value_free(words); goto oom; }
    for (size_t j = i; j < context->word_count && same_owned(context->words[j].owner, context->words[i].owner); j++) {
      if (context->words[j].name == NULL) continue;
      qa_save_value *pair = qa_save_value_array();
      qa_save_value *key = qa_save_value_string(context->words[j].name);
      qa_save_value *value = qa_save_value_number(context->words[j].value);
      bool pushed_key = false, pushed_value = false;
      if (pair != NULL && key != NULL && value != NULL) {
        pushed_key = qa_save_value_push(pair, key);
        pushed_value = pushed_key && qa_save_value_push(pair, value);
      }
      if (!pushed_value || !qa_save_value_push(words, pair)) {
        if (!pushed_key) qa_save_value_free(key);
        if (!pushed_value) qa_save_value_free(value);
        qa_save_value_free(pair); qa_save_value_free(entry); qa_save_value_free(words);
        goto oom;
      }
    }
    if (!qa_save_value_set(entry, "words", words)) { qa_save_value_free(entry); qa_save_value_free(words); goto oom; }
    if (!qa_save_value_push(players, entry)) { qa_save_value_free(entry); goto oom; }
  }
  for (size_t i = 0; i < context->ref_count; i++) {
    if (i > 0 && same_owned(context->refs[i - 1].owner, context->refs[i].owner)) continue;
    qa_save_value *entry = actor_ref_value(context->refs[i].owner.id.slot, context->refs[i].owner.id.generation);
    qa_save_value *values = qa_save_value_array();
    if (entry == NULL || values == NULL) { qa_save_value_free(entry); qa_save_value_free(values); goto oom; }
    for (size_t j = i; j < context->ref_count && same_owned(context->refs[j].owner, context->refs[i].owner); j++) {
      qa_save_value *item = qa_save_value_object();
      qa_save_value *name = qa_save_value_string(context->refs[j].name);
      qa_save_value *target = context->refs[j].has_target
          ? actor_ref_value(context->refs[j].target.slot, context->refs[j].target.generation)
          : qa_save_value_null();
      bool set_name = false, set_target = false;
      if (item != NULL && name != NULL && target != NULL) {
        set_name = qa_save_value_set(item, "name", name);
        set_target = set_name && qa_save_value_set(item, "target", target);
      }
      if (!set_target || !qa_save_value_push(values, item)) {
        if (!set_name) qa_save_value_free(name);
        if (!set_target) qa_save_value_free(target);
        qa_save_value_free(item); qa_save_value_free(entry); qa_save_value_free(values);
        goto oom;
      }
    }
    if (!qa_save_value_set(entry, "values", values)) { qa_save_value_free(entry); qa_save_value_free(values); goto oom; }
    if (!qa_save_value_push(references, entry)) { qa_save_value_free(entry); goto oom; }
  }
  for (size_t i = 0; i < context->tick_count; i++) {
    qa_save_value *tick = actor_ref_value(context->ticks[i]->actor.id.slot, context->ticks[i]->actor.id.generation);
    if (tick == NULL || !qa_save_value_push(ticks, tick)) { qa_save_value_free(tick); goto oom; }
  }
  if (!qa_save_value_set(root, "players", players)) goto oom;
  players = NULL;
  if (!qa_save_value_set(root, "references", references)) goto oom;
  references = NULL;
  if (!qa_save_value_set(root, "ticks", ticks)) goto oom;
  ticks = NULL;
  *out = qa_encode_checkpoint_value(root, out_len);
  qa_save_value_free(root);
  return *out != NULL;
oom:
  qa_save_value_free(root); qa_save_value_free(players);
  qa_save_value_free(references); qa_save_value_free(ticks);
  return false;
}

static qa_saved_actor_id saved_id(const qa_save_reader *reader) {
  qa_save_reader slot, generation;
  if (!qa_save_reader_field(reader, "slot", &slot)) qa_save_reader_fail(reader, "out of memory");
  qa_saved_actor_id id = {.slot = qa_save_reader_integer_min(&slot, 0)};
  qa_save_reader_destroy(&slot);
  if (!qa_save_reader_field(reader, "generation", &generation)) qa_save_reader_fail(reader, "out of memory");
  id.generation = qa_save_reader_integer_min(&generation, 0);
  qa_save_reader_destroy(&generation);
  return id;
}

static void q1_free_decoded_save(void *value) { qa_save_value_free(value); }
static void restore(void *ctx, const uint8_t *bytes, size_t length) {
  qa_q1ad_context *context = ctx;
  qa_session_actor_registry *actors = context->game->host.actors;
  clear_words(context);
  clear_refs(context);
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  qa_cleanup decoded_cleanup; /* a malformed save raises from the readers */
  qa_cleanup_push(&decoded_cleanup, q1_free_decoded_save, decoded);
  qa_save_reader root;
  if (!qa_save_reader_init(&root, decoded, context->extension_id)) qa_save_reader_fail(&root, "out of memory");
  qa_save_reader players;
  if (!qa_save_reader_field(&root, "players", &players)) qa_save_reader_fail(&root, "out of memory");
  size_t player_count = qa_save_reader_list_count(&players);
  for (size_t i = 0; i < player_count; i++) {
    qa_save_reader reader;
    if (!qa_save_reader_item(&players, i, &reader)) qa_save_reader_fail(&players, "out of memory");
    qa_owned_actor actor;
    if (!qa_session_actor_registry_resolve_saved(actors, saved_id(&reader), &actor))
      qa_save_reader_fail(&reader, "missing addon player");
    qa_save_reader words;
    if (!qa_save_reader_field(&reader, "words", &words)) qa_save_reader_fail(&reader, "out of memory");
    size_t word_count = qa_save_reader_list_count(&words);
    /* TS builds a fresh words Map, then playerWords.set(actor, words): a
     * repeated actor keeps its Map position and gets only the new words. */
    reset_owner_words(context, actor);
    for (size_t j = 0; j < word_count; j++) {
      qa_save_reader pair;
      if (!qa_save_reader_item(&words, j, &pair)) qa_save_reader_fail(&words, "out of memory");
      if (qa_save_reader_list_count(&pair) != 2) qa_save_reader_fail(&pair, "invalid addon player word");
      qa_save_reader key, value;
      if (!qa_save_reader_item(&pair, 0, &key) || !qa_save_reader_item(&pair, 1, &value))
        qa_save_reader_fail(&pair, "out of memory");
      const char *name = qa_save_reader_string(&key);
      double number = qa_save_reader_number(&value);
      put_word(context, actor, name, number); /* restore stores value.number() as read */
      qa_save_reader_destroy(&key); qa_save_reader_destroy(&value);
      qa_save_reader_destroy(&pair);
    }
    qa_save_reader_destroy(&words);
    qa_save_reader_destroy(&reader);
  }
  qa_save_reader_destroy(&players);
  qa_save_reader references;
  if (!qa_save_reader_field(&root, "references", &references)) qa_save_reader_fail(&root, "out of memory");
  size_t ref_count = qa_save_reader_list_count(&references);
  for (size_t i = 0; i < ref_count; i++) {
    qa_save_reader reader;
    if (!qa_save_reader_item(&references, i, &reader)) qa_save_reader_fail(&references, "out of memory");
    qa_owned_actor actor;
    if (!qa_session_actor_registry_resolve_saved(actors, saved_id(&reader), &actor))
      qa_save_reader_fail(&reader, "missing addon player references");
    qa_save_reader values;
    if (!qa_save_reader_field(&reader, "values", &values)) qa_save_reader_fail(&reader, "out of memory");
    size_t value_count = qa_save_reader_list_count(&values);
    for (size_t j = 0; j < value_count; j++) {
      qa_save_reader item, name_reader, target_reader;
      if (!qa_save_reader_item(&values, j, &item)) qa_save_reader_fail(&values, "out of memory");
      if (!qa_save_reader_field(&item, "name", &name_reader)) qa_save_reader_fail(&item, "out of memory");
      const char *name = qa_save_reader_string(&name_reader);
      if (!qa_save_reader_field(&item, "target", &target_reader)) qa_save_reader_fail(&item, "out of memory");
      if (qa_save_reader_is_null(&target_reader)) put_ref(context, actor, name, NULL);
      else {
        qa_actor_id target = qa_session_actor_registry_reference_saved(actors, saved_id(&target_reader), NULL);
        put_ref(context, actor, name, &target);
      }
      qa_save_reader_destroy(&name_reader); qa_save_reader_destroy(&target_reader);
      qa_save_reader_destroy(&item);
    }
    qa_save_reader_destroy(&values);
    qa_save_reader_destroy(&reader);
  }
  qa_save_reader_destroy(&references);
  context->tick_count = 0;
  qa_save_reader ticks;
  if (!qa_save_reader_field(&root, "ticks", &ticks)) qa_save_reader_fail(&root, "out of memory");
  size_t tick_count = qa_save_reader_list_count(&ticks);
  for (size_t i = 0; i < tick_count; i++) {
    qa_save_reader reader;
    if (!qa_save_reader_item(&ticks, i, &reader)) qa_save_reader_fail(&ticks, "out of memory");
    qa_owned_actor actor;
    bool resolved = qa_session_actor_registry_resolve_saved(actors, saved_id(&reader), &actor);
    qa_q1_actor *entity = resolved ? qa_q1_get_entity(context->game, &actor.id) : NULL;
    if (entity == NULL) qa_save_reader_fail(&reader, "missing addon frame tick actor");
    if (!grow((void **)&context->ticks, &context->tick_capacity, context->tick_count, sizeof *context->ticks, 16))
      qa_save_reader_fail(&reader, "out of memory");
    context->ticks[context->tick_count++] = entity;
    qa_save_reader_destroy(&reader);
  }
  qa_save_reader_destroy(&ticks);
  qa_save_reader_destroy(&root);
  qa_cleanup_pop(&decoded_cleanup);
  qa_save_value_free(decoded);
}

/* --- constructor: sourceItemStart / sourceBloodyNightmare closures --- */
static bool source_item_start(void *ctx, qa_q1_actor *entity, const char **place, const char **use) {
  qa_q1ad_context *context = ctx;
  if (qa_q1ad_removed_outside_coop(context, entity, true) || qa_q1ad_removed_for_runes(context, entity)) return false;
  if (context->program == QA_Q1AD_MG3 && (qa_js_to_int32(entity->spawnflags) & 4) != 0) {
    *place = "mg3:items:place";
    *use = "mg3:items:regenerate";
  } else {
    *place = "PlaceItem";
    *use = NULL;
  }
  return true;
}

static bool source_bloody_nightmare(void *ctx) {
  qa_q1ad_context *context = ctx;
  qa_q1_campaign_binding campaign = qa_q1_base_campaign(context->base);
  return (qa_js_to_int32(campaign.read_flags(campaign.ctx)) & QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE) != 0;
}

qa_q1ad_context *qa_q1ad_context_create(qa_q1_base *base, qa_q1ad_program program, qa_q1ad_services services) {
  qa_q1ad_context *context = calloc(1, sizeof *context);
  if (context == NULL) return NULL;
  context->base = base;
  context->game = qa_q1_base_game(base);
  context->program = program;
  context->services = services;
  qa_q1_services *game = context->game;
  if (program == QA_Q1AD_CTF) game->source_no_mega_rot = true;
  if (program == QA_Q1AD_MG1 || program == QA_Q1AD_MG3) {
    game->source_door_keys = true;
    game->source_grenade_fix = true;
    if (program == QA_Q1AD_MG3) {
      game->mg3_pickups = true;
      game->source_message_all_players = true;
      game->source_bloody_nightmare.ctx = context;
      game->source_bloody_nightmare.fn = source_bloody_nightmare;
    }
    game->source_item_start.ctx = context;
    game->source_item_start.fn = source_item_start;
  }
  context->release_subscription = qa_session_actor_registry_on_release(game->host.actors, on_release, context);
  (void)snprintf(context->extension_id, sizeof context->extension_id, "q1:%s:addons", qa_q1ad_program_name(program));
  context->extension = (qa_q1_state_extension){
      .ctx = context, .id = context->extension_id, .capture = capture, .restore = restore};
  qa_q1_register_state_extension(game, &context->extension);
  return context;
}

void qa_q1ad_context_destroy(qa_q1ad_context *context) {
  if (context == NULL) return;
  qa_q1ad_mg3_items_destroy(context->mg3_items);
  if (context->addon_monsters) qa_q1am_release_addon_monsters(context->game);
  qa_session_actor_registry_off_release(context->game->host.actors, context->release_subscription);
  clear_words(context);
  clear_refs(context);
  free(context->words);
  free(context->refs);
  free(context->ticks);
  free(context->spawn_use_rows);
  free(context);
}

/* --- methods --- */
bool qa_q1ad_player_word(qa_q1ad_context *context, qa_actor_id actor, const char *name, double *out) {
  qa_owned_actor owner;
  if (!resolve_owned(context, actor, &owner)) return false;
  for (size_t i = 0; i < context->word_count; i++)
    if (same_owned(context->words[i].owner, owner) && context->words[i].name != NULL &&
        strcmp(context->words[i].name, name) == 0) {
      if (out != NULL) *out = context->words[i].value;
      return true;
    }
  return false;
}

double qa_q1ad_player_number(qa_q1ad_context *context, qa_actor_id actor, const char *name) {
  double value = 0;
  return qa_q1ad_player_word(context, actor, name, &value) ? value : 0;
}

void qa_q1ad_set_player_number(qa_q1ad_context *context, qa_actor_id actor, const char *name, double value) {
  qa_owned_actor owner;
  if (!resolve_owned(context, actor, &owner)) qa_range_error("Addon player is no longer admitted");
  put_word(context, owner, name, (double)(float)value);
}

bool qa_q1ad_player_reference(qa_q1ad_context *context, qa_actor_id actor, const char *name, qa_actor_id *out) {
  qa_owned_actor owner;
  if (!resolve_owned(context, actor, &owner)) return false;
  for (size_t i = 0; i < context->ref_count; i++)
    if (same_owned(context->refs[i].owner, owner) && strcmp(context->refs[i].name, name) == 0) {
      if (context->refs[i].has_target && out != NULL) *out = context->refs[i].target;
      return context->refs[i].has_target;
    }
  return false;
}

void qa_q1ad_set_player_reference(qa_q1ad_context *context, qa_actor_id actor, const char *name,
                                  const qa_actor_id *target) {
  qa_owned_actor owner;
  if (!resolve_owned(context, actor, &owner)) qa_range_error("Addon player is no longer admitted");
  put_ref(context, owner, name, target);
}

void qa_q1ad_set_number(qa_q1ad_context *context, qa_q1_actor *entity, const char *name, double value) {
  (void)context; /* TS method reads no context state */
  char text[64];
  format_number(text, sizeof text, value);
  (void)qa_q1_actor_field_set(entity, name, text);
}

void qa_q1ad_set_vector(qa_q1ad_context *context, qa_q1_actor *entity, const char *name, qa_vec3 value) {
  (void)context; /* TS method reads no context state */
  char x[48], y[48], z[48], text[160];
  format_number(x, sizeof x, value.x);
  format_number(y, sizeof y, value.y);
  format_number(z, sizeof z, value.z);
  (void)snprintf(text, sizeof text, "%s %s %s", x, y, z);
  (void)qa_q1_actor_field_set(entity, name, text);
}

void qa_q1ad_alpha(qa_q1ad_context *context, qa_q1_actor *entity, double alpha) {
  qa_q1ad_set_number(context, entity, "alpha", alpha);
  qa_q1ad_event event = {.kind = QA_Q1AD_EVENT_ALPHA, .has_actor = true, .actor = entity->actor.id, .alpha = alpha};
  context->services.emit(context->services.ctx, &event);
}

void qa_q1ad_broadcast(qa_q1ad_context *context, const char *text) {
  qa_q1_services *game = context->game;
  size_t count = 0;
  const qa_actor_id *players = game->host.players(game->host.ctx, &count);
  for (size_t i = 0; i < count; i++) qa_q1_message(game, &players[i], text, true, NULL, 0);
}

void qa_q1ad_init_trigger(qa_q1ad_context *context, qa_q1_actor *entity) {
  if (qa_q1ad_removed_outside_coop(context, entity, false) || qa_q1ad_removed_for_runes(context, entity)) return;
  qa_q1_services *game = context->game;
  qa_vec3 angles = qa_q1_get_body(game, entity).angles;
  qa_vec3 direction = qa_q1_actor_field_get(entity, "movedir") != NULL ? qa_q1_actor_vector(entity, "movedir")
                                                                      : entity->movedir;
  entity->movedir = (angles.x == 0 && angles.y == 0 && angles.z == 0) ||
                            direction.x != 0 || direction.y != 0 || direction.z != 0
                        ? direction
                        : qa_q1_move_direction(angles, game);
  entity->solid = "trigger";
  entity->movement = "none";
  qa_q1ad_set_text(&entity->model, "");
  qa_q1_body_patch patch = {.has_angles = true, .angles = QA_Q1_ZERO};
  qa_q1_set_body(game, entity, &patch);
}

bool qa_q1ad_removed_outside_coop(qa_q1ad_context *context, qa_q1_actor *entity, bool inhibit_coop) {
  int32_t flags = qa_js_to_int32(entity->spawnflags);
  if (context->game->options.coop ? inhibit_coop && (flags & 131072) != 0 : (flags & 32768) != 0) {
    qa_q1_remove(context->game, entity);
    return true;
  }
  return false;
}

bool qa_q1ad_removed_for_runes(qa_q1ad_context *context, qa_q1_actor *entity) {
  if (context->program != QA_Q1AD_MG3) return false;
  qa_q1_campaign_binding campaign = qa_q1_base_campaign(context->base);
  double flag = 262144 * qa_js_pow(2, qa_q1ad_mg3_rune_count(campaign.read_flags(campaign.ctx)));
  if ((qa_js_to_int32(entity->spawnflags) & qa_js_to_int32(flag)) == 0) return false;
  qa_q1_remove(context->game, entity);
  return true;
}

void qa_q1ad_add_frame_tick(qa_q1ad_context *context, qa_q1_actor *entity, const char *name) {
  (void)qa_q1_actor_field_set(entity, "addon.frameTick", name);
  if (!grow((void **)&context->ticks, &context->tick_capacity, context->tick_count, sizeof *context->ticks, 16)) return;
  memmove(&context->ticks[1], &context->ticks[0], context->tick_count * sizeof *context->ticks);
  context->ticks[0] = entity;
  context->tick_count++;
}

void qa_q1ad_remove_frame_tick(qa_q1ad_context *context, qa_q1_actor *entity) {
  for (size_t i = 0; i < context->tick_count; i++)
    if (context->ticks[i] == entity) {
      memmove(&context->ticks[i], &context->ticks[i + 1], (context->tick_count - i - 1) * sizeof *context->ticks);
      context->tick_count--;
      return;
    }
}

void qa_q1ad_frame(qa_q1ad_context *context, double seconds) {
  context->frame_time = seconds;
  size_t count = context->tick_count;
  if (count == 0) return;
  qa_q1_actor **copy = malloc(count * sizeof *copy); /* [...this.frameTicks] */
  if (copy == NULL) return;
  memcpy(copy, context->ticks, count * sizeof *copy);
  qa_q1_services *game = context->game;
  for (size_t i = 0; i < count; i++) {
    const char *tick = qa_q1_actor_text(copy[i], "addon.frameTick");
    if (tick[0] != '\0' && qa_q1_live(game, copy[i]))
      qa_q1_call_action(game, copy[i], qa_q1_callback_registry_action(game->named, copy[i], tick));
  }
  free(copy);
}
