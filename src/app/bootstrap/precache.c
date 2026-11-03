/* Port of src/app/bootstrap/precache.ts. The Q1 character list is
 * precacheQ1World (content/q1/foundation/precache-world.ts) run
 * through this file's filters, in call order. GPL-2.0-or-later. */
#include "qa/app/bootstrap/precache.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/catalog/monsters.h"
#include "qa/content/catalog/weapons.h"
#include "qa/content/q2/base/player/resources.h"
#include "qa/content/q2/foundation/effect_resources.h"
#include "qa/content/q3/presentation/character_resources.h"
#include "qa/content/q3/presentation/players.h"
#include "qa/foundation/support.h"
#include "qa/app/bootstrap/application_class.h"
#include "qa/app/bootstrap/assets.h"
#include "qa/app/bootstrap/audio.h"
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/effects.h"
#include "qa/content/catalog/weapons.h"
#include "qa/content/composition/q2/index.h"
#include "qa/content/q1/foundation/entity_services.h"
#include "qa/content/q2/base/player/index.h"
#include "qa/content/q2/foundation/entity_services.h"
#include "qa/content/q2/foundation/items.h"
#include "qa/content/q2/foundation/weapons/player.h"
#include "qa/content/q2/rerelease/monsters/base_variants/medic.h"
#include "simulation/shared_simulation_internal.h"
static void push_request(qa_app_resource_requests *list, qa_content_id content, const char *path, bool owned) {
  qa_resource_request *items = realloc(list->items, (list->count + 1) * sizeof *items);
  if (items == NULL) qa_range_error("Out of memory");
  list->items = items;
  list->items[list->count].content = content;
  list->items[list->count].path = path;
  list->count++;
  if (owned) {
    char **paths = realloc(list->owned_paths, (list->owned_count + 1) * sizeof *paths);
    if (paths == NULL) qa_range_error("Out of memory");
    list->owned_paths = paths;
    list->owned_paths[list->owned_count++] = (char *)path;
  }
}
static void push_prefixed(qa_app_resource_requests *list, qa_content_id content, const char *prefix, const char *path) {
  size_t size = strlen(prefix) + strlen(path) + 1;
  char *full = malloc(size);
  if (full == NULL) qa_range_error("Out of memory");
  snprintf(full, size, "%s%s", prefix, path);
  push_request(list, content, full, true);
}
void qa_app_resource_requests_destroy(qa_app_resource_requests *requests) {
  if (requests == NULL) return;
  free(requests->items);
  for (size_t i = 0; i < requests->owned_count; i++) free(requests->owned_paths[i]);
  free(requests->owned_paths);
  for (size_t i = 0; i < requests->backing_count; i++) qa_catalog_resource_list_free(&requests->backing[i]);
  memset(requests, 0, sizeof *requests);
}
static const char *const q1_character_sounds[] = {
  "misc/h2ohit1.wav", "player/plyrjmp8.wav", "player/land.wav", "player/land2.wav",
  "player/drown1.wav", "player/drown2.wav", "player/gasp1.wav", "player/gasp2.wav",
  "player/h2odeath.wav", "player/teledth1.wav", "misc/r_tele1.wav", "misc/r_tele2.wav",
  "misc/r_tele3.wav", "misc/r_tele4.wav", "misc/r_tele5.wav", "player/gib.wav",
  "player/udeath.wav", "player/tornoff2.wav", "player/pain1.wav", "player/pain2.wav",
  "player/pain3.wav", "player/pain4.wav", "player/pain5.wav", "player/pain6.wav",
  "player/death1.wav", "player/death2.wav", "player/death3.wav", "player/death4.wav",
  "player/death5.wav", "player/axhit1.wav", "player/axhit2.wav", "player/h2ojump.wav",
  "player/slimbrn2.wav", "player/inh2o.wav", "player/inlava.wav", "misc/outwater.wav",
  "player/lburn1.wav", "player/lburn2.wav",
};
static const char *const q1_character_models[] = {
  "progs/player.mdl", "progs/eyes.mdl", "progs/h_player.mdl", "progs/gib1.mdl",
  "progs/gib2.mdl", "progs/gib3.mdl", "progs/s_bubble.spr",
};
qa_app_resource_requests qa_app_character_resource_requests(qa_app_precache_content content) {
  qa_app_resource_requests out = {0};
  qa_content_id character = content.recipe->character.definition.content;
  qa_game_family family = qa_content_catalog_product(content.catalog, character)->expectation.family;
  if (family == QA_GAME_FAMILY_Q3) {
    const char *sounds[] = {qa_q3_character_sounds.select_sound, qa_q3_character_sounds.gib_sound,
      qa_q3_character_sounds.tele_in_sound, qa_q3_character_sounds.tele_out_sound,
      qa_q3_character_sounds.respawn_sound, qa_q3_character_sounds.land_sound,
      qa_q3_character_sounds.watr_in_sound, qa_q3_character_sounds.watr_out_sound,
      qa_q3_character_sounds.watr_un_sound, qa_q3_character_sounds.jump_pad_sound};
    for (size_t i = 0; i < sizeof sounds / sizeof sounds[0]; i++) push_request(&out, character, sounds[i], false);
    for (size_t i = 0; i < qa_q3_pres_custom_sound_name_count; i++)
      push_request(&out, character, qa_q3_pres_custom_sound_names[i], false);
    return out;
  }
  if (family == QA_GAME_FAMILY_Q2) {
    for (size_t i = 0; i < qa_q2_character_model_count; i++)
      push_request(&out, character, qa_q2_character_models[i], false);
    for (size_t i = 0; i < qa_q2_character_sound_count; i++) {
      const char *path = qa_q2_character_sounds[i];
      if (path[0] == '*') push_request(&out, character, path, false);
      else push_prefixed(&out, character, "sound/", path);
    }
    return out;
  }
  for (size_t i = 0; i < sizeof q1_character_sounds / sizeof q1_character_sounds[0]; i++)
    push_prefixed(&out, character, "sound/", q1_character_sounds[i]);
  for (size_t i = 0; i < sizeof q1_character_models / sizeof q1_character_models[0]; i++)
    push_request(&out, character, q1_character_models[i], false);
  return out;
}
static char *dup_path(const char *path) {
  size_t size = strlen(path) + 1;
  char *copy = malloc(size);
  if (copy == NULL) qa_range_error("Out of memory");
  memcpy(copy, path, size);
  return copy;
}
/* TS Set<string> insertion (first-seen order). */
static void add_unique(const char ***names, size_t *count, size_t *capacity, const char *name) {
  for (size_t i = 0; i < *count; i++) if (strcmp((*names)[i], name) == 0) return;
  if (*count == *capacity) {
    *capacity = *capacity ? *capacity * 2 : 16;
    *names = realloc(*names, *capacity * sizeof **names);
    if (*names == NULL) qa_range_error("Out of memory");
  }
  (*names)[(*count)++] = name;
}
qa_app_resource_requests qa_app_native_q2_monster_resources(qa_content_id content,
    const qa_monster_source_definition *source, qa_q2_entity *const *entities, size_t entity_count) {
  qa_app_resource_requests out = {0};
  const char **classnames = NULL;
  size_t classname_count = 0, classname_cap = 0;
  for (size_t i = 0; i < entity_count; i++) add_unique(&classnames, &classname_count, &classname_cap, entities[i]->classname);
  /* Reinforcement classnames are malloc'd; kept until the requests are built. */
  qa_q2rm_medic_reinforcement_choice **choices = NULL;
  size_t *choice_counts = NULL, choice_lists = 0;
  if (source != NULL && strcmp(source->edition, "rerelease") == 0) {
    for (size_t i = 0; i < entity_count; i++) {
      const char *classname = entities[i]->classname;
      if (strcmp(classname, "monster_medic") != 0 && strcmp(classname, "monster_medic_commander") != 0) continue;
      qa_q2rm_medic_reinforcement_choice *list = NULL;
      size_t count = qa_q2rm_rerelease_medic_reinforcements(&entities[i]->spawn, &list);
      choices = realloc(choices, (choice_lists + 1) * sizeof *choices);
      choice_counts = realloc(choice_counts, (choice_lists + 1) * sizeof *choice_counts);
      if (choices == NULL || choice_counts == NULL) qa_range_error("Out of memory");
      choices[choice_lists] = list;
      choice_counts[choice_lists++] = count;
      for (size_t r = 0; r < count; r++) add_unique(&classnames, &classname_count, &classname_cap, list[r].classname);
    }
  }
  for (size_t c = 0; c < classname_count; c++) {
    const qa_monster_creature_entry *creature = source != NULL ? qa_monster_source_creature(source, classnames[c]) : NULL;
    if (creature == NULL) continue;
    for (size_t r = 0; r < creature->resource_count; r++) push_request(&out, content, creature->resources[r], false);
  }
  for (size_t i = 0; i < choice_lists; i++) qa_q2rm_medic_reinforcements_free(choices[i], choice_counts[i]);
  free(choices);
  free(choice_counts);
  free(classnames);
  return out;
}
static void append_catalog(qa_app_resource_requests *out, qa_catalog_resource_list list) {
  if (out->backing_count >= 4) qa_range_error("Out of memory");
  out->backing[out->backing_count++] = list;
  for (size_t i = 0; i < list.count; i++) push_request(out, list.values[i].content, list.values[i].path, false);
}
static void append_names(qa_app_resource_requests *out, qa_content_id content,
    const char *const *models, size_t model_count, const char *const *sounds, size_t sound_count) {
  for (size_t i = 0; i < model_count; i++)
    if (models[i][0] != '\0' && models[i][0] != '*') push_request(out, content, models[i], false);
  for (size_t i = 0; i < sound_count; i++) {
    const char *path = sounds[i];
    if (path[0] == '\0') continue;
    if (strncmp(path, "sound/", 6) == 0 || path[0] == '*') push_request(out, content, path, false);
    else push_prefixed(out, content, "sound/", path);
  }
}
static bool resource_extension_ok(const char *path) {
  static const char *const extensions[] = {".mdl", ".md2", ".md3", ".spr", ".sp2", ".wav", ".ogg"};
  size_t len = strlen(path);
  for (size_t i = 0; i < sizeof extensions / sizeof extensions[0]; i++) {
    size_t ext = strlen(extensions[i]);
    if (len < ext) continue;
    bool equal = true;
    for (size_t c = 0; c < ext; c++)
      if (tolower((unsigned char)path[len - ext + c]) != extensions[i][c]) equal = false;
    if (equal) return true;
  }
  return false;
}
qa_app_resource_requests qa_app_application_resource_requests(qa_app_precache_content content,
    qa_app_shared_simulation *simulation) {
  const qa_executable_recipe *recipe = content.recipe;
  qa_content_id native = recipe->map.entities.content;
  qa_app_resource_requests requests = qa_app_character_resource_requests(content);
  qa_catalog_resource_list list = {0};
  if (!qa_monster_resources(recipe->enemies, &list)) qa_range_error("Out of memory");
  append_catalog(&requests, list);
  memset(&list, 0, sizeof list);
  if (!qa_equipment_resources(recipe->equipment, &list)) qa_range_error("Out of memory");
  append_catalog(&requests, list);
  memset(&list, 0, sizeof list);
  if (!qa_weapon_resources(recipe->map.entities, recipe->weapons.values, recipe->weapons.count, content.catalog, &list))
    qa_range_error("Out of memory");
  append_catalog(&requests, list);
  size_t model_count = 0, sound_count = 0;
  const char *const *models = NULL, *const *sounds = NULL;
  const qa_ss_source_runtime *q1 = qa_app_shared_sim_q1_source(simulation);
  const qa_q1_services *q1_game = q1 != NULL ? q1->q1_game : NULL;
  if (q1 != NULL) {
    models = qa_q1_precache_models(q1->q1_game->precaches, &model_count);
    sounds = qa_q1_precache_sounds(q1->q1_game->precaches, &sound_count);
    append_names(&requests, native, models, model_count, sounds, sound_count);
  }
  qa_app_sim_quakec_source *qc = qa_app_shared_sim_quakec_source(simulation);
  if (qc != NULL) {
    models = qa_app_sim_quakec_source_precache_names(qc, "model", &model_count);
    sounds = qa_app_sim_quakec_source_precache_names(qc, "sound", &sound_count);
    append_names(&requests, native, models, model_count, sounds, sound_count);
  }
  qa_q1_services *q1_weapons = qa_app_shared_sim_q1_weapon_source(simulation);
  if (q1_weapons != NULL && q1_weapons != q1_game) {
    for (size_t i = 0; i < recipe->weapons.count; i++) {
      if (qa_content_catalog_product(content.catalog, recipe->weapons.values[i].content)->expectation.family != QA_GAME_FAMILY_Q1) continue;
      models = qa_q1_precache_models(q1_weapons->precaches, &model_count);
      sounds = qa_q1_precache_sounds(q1_weapons->precaches, &sound_count);
      append_names(&requests, recipe->weapons.values[i].content, models, model_count, sounds, sound_count);
      break;
    }
  }
  qa_q2_product_runtime *q2 = qa_app_shared_sim_q2_source(simulation);
  if (q2 != NULL) {
    size_t entity_count = 0;
    qa_q2_entity *const *entities = qa_q2_entity_services_entities(q2->game, &entity_count);
    const char **classnames = NULL;
    size_t classname_count = 0, classname_cap = 0;
    for (size_t i = 0; i < entity_count; i++) add_unique(&classnames, &classname_count, &classname_cap, entities[i]->classname);
    const qa_catalog_product_expectation *product = &qa_content_catalog_product(content.catalog, native)->expectation;
    const qa_monster_source_definition *source = NULL;
    for (size_t i = 0; i < qa_monster_source_count && source == NULL; i++) {
      const qa_monster_source_definition *candidate = &qa_monster_sources[i];
      qa_game_family family;
      if (qa_game_family_parse(candidate->family, &family) && family == product->family
          && strcmp(candidate->edition, product->edition) == 0 && strcmp(candidate->program, product->campaign) == 0) source = candidate;
    }
    qa_app_resource_requests monster = qa_app_native_q2_monster_resources(native, source, entities, entity_count);
    for (size_t i = 0; i < monster.count; i++) push_request(&requests, monster.items[i].content, monster.items[i].path, false);
    qa_app_resource_requests_destroy(&monster);
    size_t item_path_count = 0;
    const char *const *item_paths = qa_q2_item_module_resource_paths(q2->items, classnames, classname_count, &item_path_count);
    for (size_t i = 0; i < item_path_count; i++) push_request(&requests, native, dup_path(item_paths[i]), true);
    free(classnames);
    size_t definition_count = 0;
    const qa_q2w_definition *const *registered = qa_q2w_weapons_registered_definitions(q2->weapons, &definition_count);
    qa_catalog_q2_weapon_definition *definitions = malloc((definition_count ? definition_count : 1) * sizeof *definitions);
    if (definitions == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < definition_count; i++) definitions[i] = *registered[i];
    qa_catalog_path_list weapon_paths = {0};
    if (!qa_q2_registered_weapon_resources(definitions, definition_count, q2->services->options.edition == QA_Q2_EDITION_RERELEASE, &weapon_paths))
      qa_range_error("Out of memory");
    free(definitions);
    for (size_t i = 0; i < weapon_paths.count; i++) push_request(&requests, native, dup_path(weapon_paths.values[i]), true);
    qa_catalog_path_list_free(&weapon_paths);
    /* entities.flatMap(model..model4), entities.flatMap(noise, sound): models first, then sounds. */
    for (size_t i = 0; i < entity_count; i++) {
      const char *entity_models[] = {entities[i]->model, entities[i]->model2, entities[i]->model3, entities[i]->model4};
      for (size_t m = 0; m < 4; m++) if (entity_models[m] != NULL) append_names(&requests, native, &entity_models[m], 1, NULL, 0);
    }
    for (size_t i = 0; i < entity_count; i++) {
      const char *entity_sounds[] = {entities[i]->noise, entities[i]->sound};
      for (size_t m = 0; m < 2; m++) if (entity_sounds[m] != NULL) append_names(&requests, native, NULL, 0, &entity_sounds[m], 1);
    }
  }
  /* Transient sounds per distinct q2 content. */
  qa_content_id seen[64];
  size_t seen_count = 0;
  size_t total_before = requests.count;
  for (size_t i = 0; i <= total_before; i++) {
    qa_content_id id = i == 0 ? native : requests.items[i - 1].content;
    bool duplicate = false;
    for (size_t s = 0; s < seen_count; s++) if (strcmp(seen[s], id) == 0) duplicate = true;
    if (duplicate || seen_count >= 64) continue;
    seen[seen_count++] = id;
    const qa_catalog_product_expectation *product =
        &qa_content_catalog_product(content.catalog, id)->expectation;
    if (product->family != QA_GAME_FAMILY_Q2) continue;
    append_names(&requests, id, NULL, 0, QA_Q2_TRANSIENT_SOUNDS, QA_Q2_TRANSIENT_SOUNDS_COUNT);
    if (strcmp(product->edition, "rerelease") == 0 || strcmp(product->campaign, "rogue") == 0)
      append_names(&requests, id, NULL, 0, QA_Q2_ROGUE_TRANSIENT_SOUNDS, QA_Q2_ROGUE_TRANSIENT_SOUNDS_COUNT);
  }
  /* Filter + dedupe by content\0path, first wins (TS Map). */
  qa_app_resource_requests filtered = {0};
  filtered.backing_count = requests.backing_count;
  memcpy(filtered.backing, requests.backing, sizeof filtered.backing);
  requests.backing_count = 0;
  for (size_t i = 0; i < requests.count; i++) {
    if (!resource_extension_ok(requests.items[i].path)) continue;
    bool duplicate = false;
    for (size_t f = 0; f < filtered.count; f++)
      if (strcmp(filtered.items[f].content, requests.items[i].content) == 0
          && strcmp(filtered.items[f].path, requests.items[i].path) == 0) duplicate = true;
    if (!duplicate) push_request(&filtered, requests.items[i].content, requests.items[i].path, false);
  }
  filtered.owned_paths = requests.owned_paths;
  filtered.owned_count = requests.owned_count;
  requests.owned_paths = NULL; requests.owned_count = 0;
  qa_app_resource_requests_destroy(&requests);
  return filtered;
}
static bool audio_extension(const char *path) {
  size_t len = strlen(path);
  if (len < 4 || path[len - 4] != '.') return false;
  char ext[4];
  for (size_t i = 0; i < 3; i++) ext[i] = (char)tolower((unsigned char)path[len - 3 + i]);
  ext[3] = '\0';
  return strcmp(ext, "wav") == 0 || strcmp(ext, "ogg") == 0;
}
typedef struct preload_call {
  qa_app_application_audio *audio;
  qa_app_application_effects *effects;
  const qa_resource_request *request;
  const char *voice; /* NULL = no selected model */
  bool sound;
} preload_call;
static void preload_run(void *ctx) {
  preload_call *call = ctx;
  if (call->sound) qa_app_application_audio_preload_sound(call->audio, call->request->content, call->request->path, call->voice);
  else qa_app_application_effects_preload_model(call->effects, call->request->content, call->request->path);
}
static void footsteps_run(void *ctx) { qa_app_application_audio_preload_character_footsteps(ctx); }
static void print_skipped(void (*print)(void *ctx, const char *message), void *print_ctx, const char *prefix, const char *message) {
  size_t size = strlen(prefix) + strlen(message) + 3;
  char *text = malloc(size);
  if (text == NULL) qa_range_error("Out of memory");
  snprintf(text, size, "%s: %s\n", prefix, message);
  print(print_ctx, text);
  free(text);
}
void qa_app_prepare_application_resources(qa_app_precache_content content, qa_app_shared_simulation *simulation,
    qa_app_application_audio *audio, qa_app_application_effects *effects,
    void (*progress)(void *ctx, const char *message), void *progress_ctx,
    void (*print)(void *ctx, const char *message), void *print_ctx) {
  qa_app_resource_requests requests = qa_app_application_resource_requests(content, simulation);
  /* q2Skins: [...q2Source()?.players.states.values()].map(player => player.skin) */
  const char **skins = NULL;
  size_t skin_count = 0, skin_cap = 0;
  qa_q2_product_runtime *q2 = qa_app_shared_sim_q2_source(simulation);
  size_t state_count = q2 != NULL ? qa_q2_players_state_count(q2->players) : 0;
  for (size_t i = 0; i < state_count; i++) {
    qa_actor_id actor;
    qa_q2_base_player_state *state = NULL;
    if (!qa_q2_players_state_at(q2->players, i, &actor, &state)) continue;
    if (skin_count == skin_cap) {
      skin_cap = skin_cap ? skin_cap * 2 : 4;
      skins = realloc(skins, skin_cap * sizeof *skins);
      if (skins == NULL) qa_range_error("Out of memory");
    }
    skins[skin_count++] = state->skin;
  }
  for (size_t i = 0; i < requests.count; i++) {
    char message[128];
    snprintf(message, sizeof message, "Preparing resources %zu/%zu...", i + 1, requests.count);
    progress(progress_ctx, message);
    const qa_resource_request *request = &requests.items[i];
    preload_call call = {audio, effects, request, NULL, audio_extension(request->path)};
    qa_caught_error caught;
    bool ok = true;
    if (call.sound && request->path[0] == '*'
        && qa_content_catalog_product(content.catalog, request->content)->expectation.family == QA_GAME_FAMILY_Q2) {
      /* new Set(q2Skins.length === 0 ? ["male"] : q2Skins), voice = skin.split("/")[0] || "male" */
      const char *male[1] = {"male"};
      const char *const *voices = skin_count == 0 ? male : skins;
      size_t voice_count = skin_count == 0 ? 1 : skin_count;
      for (size_t s = 0; s < voice_count && ok; s++) {
        bool duplicate = false;
        for (size_t d = 0; d < s; d++) if (strcmp(voices[d], voices[s]) == 0) duplicate = true;
        if (duplicate) continue;
        const char *slash = strchr(voices[s], '/');
        size_t length = slash != NULL ? (size_t)(slash - voices[s]) : strlen(voices[s]);
        char *voice = length == 0 ? dup_path("male") : malloc(length + 1);
        if (voice == NULL) qa_range_error("Out of memory");
        if (length != 0) { memcpy(voice, voices[s], length); voice[length] = '\0'; }
        call.voice = voice;
        qa_cleanup node;
        qa_cleanup_push(&node, free, voice);
        ok = qa_try(preload_run, &call, &caught);
        qa_cleanup_pop(&node);
        free(voice);
      }
    } else {
      ok = qa_try(preload_run, &call, &caught);
    }
    if (!ok) {
      size_t size = strlen(request->content) + strlen(request->path) + 64;
      char *prefix = malloc(size);
      if (prefix == NULL) qa_range_error("Out of memory");
      snprintf(prefix, size, "Optional resource preload skipped: %s/%s", request->content, request->path);
      print_skipped(print, print_ctx, prefix, caught.message);
      free(prefix);
    }
  }
  free(skins);
  qa_app_resource_requests_destroy(&requests);
  qa_caught_error caught;
  if (!qa_try(footsteps_run, audio, &caught)) print_skipped(print, print_ctx, "Optional character footsteps preload skipped", caught.message);
}

/* Application's prepareApplicationResources call: content is assets.content, progress is host.loading?.stage. */
static void loading_stage(void *ctx, const char *message) {
  const qa_application_loading *loading = ctx;
  if (loading != NULL && loading->stage != NULL) loading->stage(loading->ctx, message);
}
void qa_app_prepare_world_resources(qa_app_application_assets *assets, qa_app_shared_simulation *simulation, qa_app_application_audio *audio,
    qa_app_application_effects *effects, const qa_application_loading *loading, void (*print)(void *ctx, const char *text), void *print_ctx) {
  qa_app_loaded_content *loaded = qa_app_application_assets_content(assets);
  qa_app_precache_content content = {qa_app_loaded_content_catalog(loaded), qa_app_loaded_content_recipe(loaded)};
  qa_app_prepare_application_resources(content, simulation, audio, effects, loading_stage, (void *)loading, print, print_ctx);
}
