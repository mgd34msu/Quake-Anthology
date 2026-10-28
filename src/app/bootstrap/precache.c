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
qa_app_resource_requests qa_app_native_q2_monster_resources(qa_content_id content,
    const qa_monster_source_definition *source,
    const qa_app_q2_precache_entity *entities, size_t entity_count) {
  qa_app_resource_requests out = {0};
  /* Unique classnames in first-seen order (TS Set). Medic
   * reinforcements need spawn fields; the host supplies them via
   * spawn_text parsing in the composition root - classnames from
   * reinforcements are appended by qa_app_application_resource_requests'
   * caller when available (see report). */
  const char **classnames = calloc(entity_count == 0 ? 1 : entity_count, sizeof *classnames);
  if (classnames == NULL) qa_range_error("Out of memory");
  size_t classname_count = 0;
  for (size_t i = 0; i < entity_count; i++) {
    bool seen = false;
    for (size_t c = 0; c < classname_count; c++)
      if (strcmp(classnames[c], entities[i].classname) == 0) seen = true;
    if (!seen) classnames[classname_count++] = entities[i].classname;
  }
  for (size_t c = 0; c < classname_count; c++) {
    const qa_monster_creature_entry *creature =
        source != NULL ? qa_monster_source_creature(source, classnames[c]) : NULL;
    if (creature == NULL) continue;
    for (size_t r = 0; r < creature->resource_count; r++)
      push_request(&out, content, creature->resources[r], false);
  }
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
    const qa_app_precache_source *simulation) {
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
  const char *const *models = NULL, *const *sounds = NULL;
  size_t model_count = 0, sound_count = 0;
  const void *q1_game = NULL;
  bool has_q1 = simulation->q1_precaches(simulation->ctx, &models, &model_count, &sounds, &sound_count, &q1_game);
  if (has_q1) append_names(&requests, native, models, model_count, sounds, sound_count);
  if (simulation->quakec_precaches(simulation->ctx, &models, &model_count, &sounds, &sound_count))
    append_names(&requests, native, models, model_count, sounds, sound_count);
  const void *weapon_game = NULL;
  if (simulation->q1_weapon_precaches(simulation->ctx, &models, &model_count, &sounds, &sound_count, &weapon_game)
      && weapon_game != q1_game) {
    for (size_t i = 0; i < recipe->weapons.count; i++) {
      if (qa_content_catalog_product(content.catalog, recipe->weapons.values[i].content)->expectation.family == QA_GAME_FAMILY_Q1) {
        append_names(&requests, recipe->weapons.values[i].content, models, model_count, sounds, sound_count);
        break;
      }
    }
  }
  const qa_app_q2_precache_entity *entities = NULL;
  size_t entity_count = 0;
  const char *const *item_paths = NULL;
  size_t item_path_count = 0;
  bool rerelease = false;
  const char *const *skins = NULL;
  size_t skin_count = 0;
  if (simulation->q2_entities(simulation->ctx, &entities, &entity_count, &item_paths, &item_path_count, &rerelease, &skins, &skin_count)) {
    (void)skins; (void)skin_count;
    const qa_catalog_product_expectation *product =
        &qa_content_catalog_product(content.catalog, native)->expectation;
    const qa_monster_source_definition *source = NULL;
    for (size_t i = 0; i < qa_monster_source_count; i++) {
      const qa_monster_source_definition *candidate = &qa_monster_sources[i];
      qa_game_family family;
      if (qa_game_family_parse(candidate->family, &family) && family == product->family
          && strcmp(candidate->edition, product->edition) == 0
          && strcmp(candidate->program, product->campaign) == 0) source = candidate;
    }
    qa_app_resource_requests monster = qa_app_native_q2_monster_resources(native, source, entities, entity_count);
    for (size_t i = 0; i < monster.count; i++) push_request(&requests, monster.items[i].content, monster.items[i].path, false);
    qa_app_resource_requests_destroy(&monster);
    for (size_t i = 0; i < item_path_count; i++) push_request(&requests, native, item_paths[i], false);
    const char *const *weapon_paths = NULL;
    size_t weapon_path_count = 0;
    if (simulation->q2_weapon_paths(simulation->ctx, rerelease, &weapon_paths, &weapon_path_count))
      for (size_t i = 0; i < weapon_path_count; i++) push_request(&requests, native, weapon_paths[i], false);
    for (size_t i = 0; i < entity_count; i++) {
      const char *entity_models[] = {entities[i].model, entities[i].model2, entities[i].model3, entities[i].model4};
      const char *entity_sounds[] = {entities[i].noise, entities[i].sound};
      const char *models_or_empty[4];
      const char *sounds_or_empty[2];
      for (size_t m = 0; m < 4; m++) models_or_empty[m] = entity_models[m] != NULL ? entity_models[m] : "";
      for (size_t s = 0; s < 2; s++) sounds_or_empty[s] = entity_sounds[s] != NULL ? entity_sounds[s] : "";
      append_names(&requests, native, models_or_empty, 4, sounds_or_empty, 2);
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
  return (len >= 4 && (strcmp(path + len - 4, ".wav") == 0 || strcmp(path + len - 4, ".ogg") == 0))
      || (len >= 4 && tolower((unsigned char)path[len - 3]) == 'w' && tolower((unsigned char)path[len - 2]) == 'a' && tolower((unsigned char)path[len - 1]) == 'v' && path[len - 4] == '.')
      || (len >= 4 && path[len - 4] == '.' && tolower((unsigned char)path[len - 3]) == 'o' && tolower((unsigned char)path[len - 2]) == 'g' && tolower((unsigned char)path[len - 1]) == 'g');
}
void qa_app_prepare_application_resources(qa_app_precache_content content,
    const qa_app_precache_source *simulation, const qa_app_prepare_resources_host *host) {
  qa_app_resource_requests requests = qa_app_application_resource_requests(content, simulation);
  const char *const *skins = NULL;
  size_t skin_count = host->q2_skins(host->ctx, &skins);
  for (size_t i = 0; i < requests.count; i++) {
    char message[128];
    snprintf(message, sizeof message, "Preparing resources %zu/%zu...", i + 1, requests.count);
    host->progress(host->ctx, message);
    const qa_resource_request *request = &requests.items[i];
    const char *error = NULL;
    if (audio_extension(request->path)) {
      if (request->path[0] == '*'
          && qa_content_catalog_product(content.catalog, request->content)->expectation.family == QA_GAME_FAMILY_Q2) {
        if (skin_count == 0) {
          error = host->preload_sound(host->ctx, request->content, request->path, "male");
        } else {
          for (size_t s = 0; s < skin_count && error == NULL; s++) {
            bool duplicate = false;
            for (size_t d = 0; d < s; d++) if (strcmp(skins[d], skins[s]) == 0) duplicate = true;
            if (duplicate) continue;
            char voice[256];
            const char *slash = strchr(skins[s], '/');
            size_t len = slash != NULL ? (size_t)(slash - skins[s]) : strlen(skins[s]);
            if (len == 0) { error = host->preload_sound(host->ctx, request->content, request->path, "male"); continue; }
            if (len >= sizeof voice) len = sizeof voice - 1;
            memcpy(voice, skins[s], len); voice[len] = '\0';
            error = host->preload_sound(host->ctx, request->content, request->path, voice);
          }
        }
      } else {
        error = host->preload_sound(host->ctx, request->content, request->path, NULL);
      }
    } else {
      error = host->preload_model(host->ctx, request->content, request->path);
    }
    if (error != NULL) {
      size_t size = strlen(request->content) + strlen(request->path) + strlen(error) + 64;
      char *text = malloc(size);
      if (text == NULL) qa_range_error("Out of memory");
      snprintf(text, size, "Optional resource preload skipped: %s/%s: %s\n", request->content, request->path, error);
      host->print(host->ctx, text);
      free(text);
    }
  }
  qa_app_resource_requests_destroy(&requests);
  const char *error = host->preload_footsteps(host->ctx);
  if (error != NULL) {
    size_t size = strlen(error) + 64;
    char *text = malloc(size);
    if (text == NULL) qa_range_error("Out of memory");
    snprintf(text, size, "Optional character footsteps preload skipped: %s\n", error);
    host->print(host->ctx, text);
    free(text);
  }
}
